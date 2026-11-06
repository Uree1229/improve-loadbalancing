// SPDX-License-Identifier: GPL-2.0
/* Event-only affinity recovery; no hook in ordinary SCHED_SOFTIRQ. */
#include "sched.h"
#include <linux/interrupt.h>
#include <linux/irq_work.h>
#include <linux/sysctl.h>

#ifdef CONFIG_IRQ_WORK
/* Default off; the runtime control is only read on the event path. */
static int affinity_balance_enabled;

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
	{ }
};

static int __init sched_affinity_sysctl_init(void)
{
	return register_sysctl("kernel", affinity_balance_sysctls) ?
		0 : -ENOMEM;
}
late_initcall(sched_affinity_sysctl_init);
#endif

struct affinity_balance_work {
	struct irq_work work;
	struct tasklet_struct tasklet;
	int cpu;
};

static DEFINE_PER_CPU_SHARED_ALIGNED(struct affinity_balance_work,
				    affinity_balance_work);
DEFINE_PER_CPU(unsigned long, sched_affinity_hints);
DEFINE_PER_CPU(unsigned long, sched_affinity_queues);
DEFINE_PER_CPU(unsigned long, sched_affinity_works);
DEFINE_PER_CPU(unsigned long, sched_affinity_resets);

static void affinity_balance_tasklet(unsigned long data)
{
	struct affinity_balance_work *state = (void *)data;
	int cpu = smp_processor_id();

	/* A queued notification may outlive a userspace disable. */
	if (!READ_ONCE(affinity_balance_enabled))
		return;
	/* CPU-dead tasklets can be taken over by a different CPU. */
	if (unlikely(state->cpu != cpu || !cpu_active(cpu)))
		return;
	sched_affinity_force_balance();
}

static void affinity_balance_irq(struct irq_work *work)
{
	struct affinity_balance_work *state;

	state = container_of(work, struct affinity_balance_work, work);
	this_cpu_inc(sched_affinity_works);
	if (likely(cpu_active(smp_processor_id())))
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
#endif
