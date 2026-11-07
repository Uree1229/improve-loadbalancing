// SPDX-License-Identifier: GPL-2.0
/* Event-only recovery with a bounded retry on a still-idle destination. */
#include "sched.h"
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/irq_work.h>
#include <linux/sysctl.h>
#include <linux/timekeeping.h>

#ifdef CONFIG_IRQ_WORK
#define AFFINITY_RETRY_LIMIT 2
#define AFFINITY_RETRY_DELAY_NS NSEC_PER_MSEC
#define AFFINITY_RETRY_WINDOW_NS (5 * NSEC_PER_MSEC)

static int affinity_balance_enabled;
static int affinity_retry_enabled;
static int affinity_pull_enabled;
static int affinity_push_enabled;
static int affinity_idle_force_enabled;

#ifdef CONFIG_SYSCTL
static struct ctl_table affinity_balance_sysctls[] = {
	{
		.procname	= "sched_affinity_rebalance",
		.data		= &affinity_balance_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "sched_affinity_retry",
		.data		= &affinity_retry_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "sched_affinity_pull",
		.data		= &affinity_pull_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "sched_affinity_push",
		.data		= &affinity_push_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "sched_affinity_idle_force",
		.data		= &affinity_idle_force_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{ }
};

#endif

struct affinity_balance_work {
	struct irq_work work;
	struct tasklet_struct tasklet;
	struct hrtimer retry;
	u64 retry_deadline;
	unsigned int retries_left;
	bool notified;
	bool retry_pending;
	int cpu;
};

static DEFINE_PER_CPU_SHARED_ALIGNED(struct affinity_balance_work,
				    affinity_balance_work);
DEFINE_PER_CPU(unsigned long, sched_affinity_hints);
DEFINE_PER_CPU(unsigned long, sched_affinity_queues);
DEFINE_PER_CPU(unsigned long, sched_affinity_works);
DEFINE_PER_CPU(unsigned long, sched_affinity_resets);
DEFINE_PER_CPU(unsigned long, sched_affinity_retry_arms);
DEFINE_PER_CPU(unsigned long, sched_affinity_retry_fires);
DEFINE_PER_CPU(unsigned long, sched_affinity_retry_runs);
DEFINE_PER_CPU(unsigned long, sched_affinity_retry_skips);
DEFINE_PER_CPU(unsigned long, sched_affinity_pull_attempts);
DEFINE_PER_CPU(unsigned long, sched_affinity_pull_moved);
DEFINE_PER_CPU(unsigned long, sched_affinity_push_attempts);
DEFINE_PER_CPU(unsigned long, sched_affinity_push_moved);
DEFINE_PER_CPU(unsigned long, sched_affinity_force_skips);

static enum hrtimer_restart affinity_balance_retry(struct hrtimer *timer)
{
	struct affinity_balance_work *state;
	int cpu = smp_processor_id();

	state = container_of(timer, struct affinity_balance_work, retry);
	if (!READ_ONCE(affinity_balance_enabled) ||
	    !READ_ONCE(affinity_retry_enabled) || state->cpu != cpu ||
	    !cpu_active(cpu))
		return HRTIMER_NORESTART;
	if (!state->retries_left || ktime_get_ns() >= state->retry_deadline ||
	    !idle_cpu(cpu)) {
		state->retries_left = 0;
		this_cpu_inc(sched_affinity_retry_skips);
		return HRTIMER_NORESTART;
	}
	state->retry_pending = true;
	this_cpu_inc(sched_affinity_retry_fires);
	/* Keep load_balance() in tasklet context, never in the timer IRQ. */
	tasklet_schedule(&state->tasklet);
	return HRTIMER_NORESTART;
}

static void affinity_balance_arm_retry(struct affinity_balance_work *state)
{
	unsigned long flags;
	int cpu = smp_processor_id();

	if (!READ_ONCE(affinity_retry_enabled))
		return;
	/* IRQ work and timer callbacks update this CPU's retry state. */
	local_irq_save(flags);
	if (!READ_ONCE(affinity_balance_enabled) || !cpu_active(cpu) ||
	    state->cpu != cpu || !idle_cpu(cpu) || !state->retries_left ||
	    ktime_get_ns() >= state->retry_deadline) {
		state->retries_left = 0;
		goto out;
	}
	if (!hrtimer_active(&state->retry)) {
		hrtimer_start(&state->retry,
			      ns_to_ktime(AFFINITY_RETRY_DELAY_NS),
			      HRTIMER_MODE_REL_PINNED);
		this_cpu_inc(sched_affinity_retry_arms);
	}
out:
	local_irq_restore(flags);
}

static void affinity_balance_tasklet(unsigned long data)
{
	struct affinity_balance_work *state = (void *)data;
	unsigned long flags;
	int cpu = smp_processor_id();
	bool notified, retry;

	if (!READ_ONCE(affinity_balance_enabled))
		return;
	if (unlikely(state->cpu != cpu || !cpu_active(cpu)))
		return;
	local_irq_save(flags);
	notified = state->notified;
	retry = state->retry_pending;
	state->notified = false;
	state->retry_pending = false;
	if (!notified) {
		if (!retry || !READ_ONCE(affinity_retry_enabled) ||
		    !state->retries_left || !idle_cpu(cpu) ||
		    ktime_get_ns() >= state->retry_deadline) {
			state->retries_left = 0;
			if (retry)
				this_cpu_inc(sched_affinity_retry_skips);
			local_irq_restore(flags);
			return;
		}
		state->retries_left--;
		this_cpu_inc(sched_affinity_retry_runs);
	}
	local_irq_restore(flags);
	if (READ_ONCE(affinity_push_enabled))
		sched_affinity_push_idle();
	/* A queued task or active caller already makes this CPU busy. */
	if (!READ_ONCE(affinity_idle_force_enabled) || idle_cpu(cpu))
		sched_affinity_force_balance();
	else
		this_cpu_inc(sched_affinity_force_skips);
	if (READ_ONCE(affinity_pull_enabled))
		sched_affinity_pull_idle();
	affinity_balance_arm_retry(state);
}

static void affinity_balance_irq(struct irq_work *work)
{
	struct affinity_balance_work *state;
	unsigned long flags;
	int cpu = smp_processor_id();

	state = container_of(work, struct affinity_balance_work, work);
	this_cpu_inc(sched_affinity_works);
	if (unlikely(state->cpu != cpu || !cpu_active(cpu)))
		return;
	local_irq_save(flags);
	state->notified = true;
	if (READ_ONCE(affinity_balance_enabled) &&
	    READ_ONCE(affinity_retry_enabled)) {
		state->retries_left = AFFINITY_RETRY_LIMIT;
		state->retry_deadline = ktime_get_ns() +
					AFFINITY_RETRY_WINDOW_NS;
	} else {
		state->retries_left = 0;
	}
	local_irq_restore(flags);
	tasklet_schedule(&state->tasklet);
}

void sched_affinity_rebalance(struct rq *source,
			      const struct cpumask *allowed)
{
	int cpu;

	lockdep_assert_held(&source->lock);
	if (!READ_ONCE(affinity_balance_enabled))
		return;
	this_cpu_inc(sched_affinity_hints);
	rcu_read_lock();
	for_each_cpu_and(cpu, allowed, cpu_active_mask) {
		struct rq *target = cpu_rq(cpu);
		struct affinity_balance_work *state;

		if (cpu == cpu_of(source) ||
		    !rcu_dereference_sched(target->sd) ||
		    !cpumask_test_cpu(cpu_of(source), target->rd->span))
			continue;
		state = &per_cpu(affinity_balance_work, cpu);
		if (irq_work_queue_on(&state->work, cpu))
			this_cpu_inc(sched_affinity_queues);
	}
	rcu_read_unlock();
}

void __init sched_affinity_init(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct affinity_balance_work *state;

		state = &per_cpu(affinity_balance_work, cpu);
		state->cpu = cpu;
		init_irq_work(&state->work, affinity_balance_irq);
		tasklet_init(&state->tasklet, affinity_balance_tasklet,
			     (unsigned long)state);
	}
}

static int __init sched_affinity_controls_init(void)
{
	int cpu;

	/* sched_init() runs before the timer subsystem is initialized. */
	for_each_possible_cpu(cpu) {
		struct affinity_balance_work *state;

		state = &per_cpu(affinity_balance_work, cpu);
		hrtimer_init(&state->retry, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		state->retry.function = affinity_balance_retry;
	}
#ifdef CONFIG_SYSCTL
	/* Publish the controls only after every retry timer is initialized. */
	return register_sysctl("kernel", affinity_balance_sysctls) ?
		0 : -ENOMEM;
#else
	return 0;
#endif
}
late_initcall(sched_affinity_controls_init);
#endif
