#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* Separate diagnostic: same arithmetic as lb_bench, no external polling. */
#define MAX_EVENTS 512
struct event { uint64_t previous_ns, ns; int cpu; };
struct worker {
    unsigned id, count;
    uint64_t steps, words, checksum, finished_ns, max_gap_ns;
    long tid;
    double cpu_s;
    struct event events[MAX_EVENTS];
};
static pthread_barrier_t ready, start, started;
static _Atomic uint64_t release_epoch;

static void fail(const char *what)
{
    perror(what);
    exit(EXIT_FAILURE);
}

static uint64_t now_ns(clockid_t clock)
{
    struct timespec ts;
    if (clock_gettime(clock, &ts)) fail("clock_gettime");
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
}

static uint64_t number(const char *text)
{
    char *end;
    errno = 0;
    uint64_t value = strtoull(text, &end, 10);
    if (errno || !*text || *text == '-' || *end) {
        fprintf(stderr, "Invalid integer: %s\n", text);
        exit(EXIT_FAILURE);
    }
    return value;
}

static void record(struct worker *w, uint64_t previous, uint64_t ns, int cpu)
{
    if (w->count == MAX_EVENTS) {
        fprintf(stderr, "CPU event buffer overflow\n");
        exit(EXIT_FAILURE);
    }
    w->events[w->count++] = (struct event){previous, ns, cpu};
}

static void *work(void *arg)
{
    struct worker *w = arg;
    uint64_t *buffer = NULL;
    int rc = posix_memalign((void **)&buffer, 64, w->words * sizeof(*buffer));
    if (rc) { errno = rc; fail("posix_memalign"); }
    for (uint64_t i = 0; i < w->words; i++)
        buffer[i] = (i + 1) * UINT64_C(0x9e3779b97f4a7c15);
    w->tid = syscall(SYS_gettid);
    pthread_barrier_wait(&ready);
    pthread_barrier_wait(&start);
    uint64_t previous = now_ns(CLOCK_MONOTONIC);
    int last_cpu = sched_getcpu();
    if (last_cpu < 0) fail("sched_getcpu");
    record(w, previous, previous, last_cpu);
    pthread_barrier_wait(&started);
    uint64_t cpu_start = now_ns(CLOCK_THREAD_CPUTIME_ID);
    uint64_t state = UINT64_C(0x123456789abcdef) + w->id;
    for (uint64_t done = 0; done < w->steps;) {
        uint64_t batch = w->steps - done;
        if (batch > 262144) batch = 262144;
        for (uint64_t i = 0; i < batch; i++) {
            uint64_t index = (state >> 17) & (w->words - 1);
            state = state * UINT64_C(2862933555777941757) + buffer[index] + 1;
            buffer[index] = state ^ (state >> 23);
        }
        done += batch;
        int cpu = sched_getcpu();
        uint64_t ns = now_ns(CLOCK_MONOTONIC);
        if (cpu < 0) fail("sched_getcpu");
        if (atomic_load_explicit(&release_epoch, memory_order_relaxed) && ns - previous > w->max_gap_ns)
            w->max_gap_ns = ns - previous;
        if (cpu != last_cpu) {
            record(w, previous, ns, cpu);
            last_cpu = cpu;
        }
        previous = ns;
    }
    w->finished_ns = now_ns(CLOCK_MONOTONIC);
    w->cpu_s = (now_ns(CLOCK_THREAD_CPUTIME_ID) - cpu_start) / 1e9;
    w->checksum = state ^ buffer[0] ^ buffer[w->words - 1];
    free(buffer);
    return NULL;
}

static void snapshot(const char *dir, const char *name)
{
    char path[4096], buffer[4096];
    if (snprintf(path, sizeof(path), "%s/%s.sched_debug", dir, name) >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        fail("snapshot path");
    }
    FILE *in = fopen("/proc/sched_debug", "r");
    FILE *out = fopen(path, "wx");
    if (!in || !out) fail("snapshot fopen");
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), in)))
        if (fwrite(buffer, 1, n, out) != n) fail("snapshot fwrite");
    if (ferror(in)) fail("snapshot fread");
    fclose(in);
    if (fclose(out)) fail("snapshot fclose");
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "Usage: %s WORKERS STEPS BUFFER_KIB HOLD_MS SNAPSHOT_DIR_OR_DASH\n", argv[0]);
        return EXIT_FAILURE;
    }
    uint64_t n = number(argv[1]), steps = number(argv[2]), kib = number(argv[3]);
    uint64_t hold_ms = number(argv[4]);
    cpu_set_t allowed, one, parent;
    if (sched_getaffinity(0, sizeof(allowed), &allowed)) fail("sched_getaffinity");
    unsigned ncpu = CPU_COUNT(&allowed);
    if (ncpu < 2 || ncpu > 64 || n < ncpu || n > 64 || steps < n ||
        kib < 4 || kib > 16384 || (kib & (kib - 1)) || hold_ms > 5000) {
        fprintf(stderr, "Invalid CPU/workload/hold parameters\n");
        return EXIT_FAILURE;
    }
    int first = -1, last = -1;
    for (int c = 0; c < CPU_SETSIZE; c++)
        if (CPU_ISSET(c, &allowed)) { if (first < 0) first = c; last = c; }
    CPU_ZERO(&one); CPU_SET(first, &one);
    CPU_ZERO(&parent); CPU_SET(last, &parent);
    if (sched_setaffinity(0, sizeof(parent), &parent)) fail("parent affinity");
    struct worker *workers = calloc(n, sizeof(*workers));
    pthread_t *threads = calloc(n, sizeof(*threads));
    if (!workers || !threads) fail("calloc");
    int rc = pthread_barrier_init(&ready, NULL, n + 1);
    if (!rc) rc = pthread_barrier_init(&start, NULL, n + 1);
    if (!rc) rc = pthread_barrier_init(&started, NULL, n + 1);
    if (rc) { errno = rc; fail("barrier init"); }
    pthread_attr_t attr;
    if ((rc = pthread_attr_init(&attr)) ||
        (rc = pthread_attr_setaffinity_np(&attr, sizeof(one), &one))) {
        errno = rc; fail("thread attr");
    }
    for (unsigned i = 0; i < n; i++) {
        workers[i].id = i;
        workers[i].steps = steps / n + (i < steps % n);
        workers[i].words = kib * 1024 / sizeof(uint64_t);
        if ((rc = pthread_create(&threads[i], &attr, work, &workers[i]))) {
            errno = rc; fail("pthread_create");
        }
    }
    pthread_attr_destroy(&attr);
    pthread_barrier_wait(&ready);
    pthread_barrier_wait(&start);
    pthread_barrier_wait(&started);
    uint64_t begin = now_ns(CLOCK_MONOTONIC);
    uint64_t wake_ns = begin + hold_ms * UINT64_C(1000000);
    struct timespec wake = {(time_t)(wake_ns / 1000000000), (long)(wake_ns % 1000000000)};
    do { rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wake, NULL); } while (rc == EINTR);
    if (rc) { errno = rc; fail("clock_nanosleep"); }
    uint64_t snapshot_begin = now_ns(CLOCK_MONOTONIC);
    if (strcmp(argv[5], "-")) snapshot(argv[5], "before-release");
    uint64_t release_start = now_ns(CLOCK_MONOTONIC);
    atomic_store_explicit(&release_epoch, release_start, memory_order_relaxed);
    uint64_t release_times[64][2];
    for (unsigned i = 0; i < n; i++) {
        cpu_set_t check;
        if ((rc = pthread_getaffinity_np(threads[i], sizeof(check), &check))) {
            errno = rc; fail("get worker affinity");
        }
        if (!CPU_EQUAL(&one, &check)) { fprintf(stderr, "Unexpected initial affinity\n"); return 1; }
        release_times[i][0] = now_ns(CLOCK_MONOTONIC);
        if ((rc = pthread_setaffinity_np(threads[i], sizeof(allowed), &allowed))) {
            errno = rc; fail("widen worker affinity");
        }
        release_times[i][1] = now_ns(CLOCK_MONOTONIC);
        if ((rc = pthread_getaffinity_np(threads[i], sizeof(check), &check)) || !CPU_EQUAL(&allowed, &check)) {
            errno = rc ? rc : EINVAL; fail("verify widened affinity");
        }
    }
    uint64_t release_end = now_ns(CLOCK_MONOTONIC);
    uint64_t snapshot_after_begin = release_end;
    if (strcmp(argv[5], "-")) snapshot(argv[5], "after-release");
    uint64_t snapshot_after_end = now_ns(CLOCK_MONOTONIC);
    for (unsigned i = 0; i < n; i++)
        if ((rc = pthread_join(threads[i], NULL))) { errno = rc; fail("pthread_join"); }
    uint64_t end = now_ns(CLOCK_MONOTONIC);
    printf("{\"schema_version\":1,\"workers\":%" PRIu64 ",\"total_steps\":%" PRIu64
           ",\"buffer_kib\":%" PRIu64 ",\"hold_ms\":%" PRIu64 ",\"parent_cpu\":%d,\"cpus\":[", n, steps, kib, hold_ms, last);
    unsigned printed = 0;
    for (int c = 0; c < CPU_SETSIZE; c++)
        if (CPU_ISSET(c, &allowed)) printf("%s%d", printed++ ? "," : "", c);
    printf("],\"begin_ns\":%" PRIu64 ",\"end_ns\":%" PRIu64 ",\"snapshot_before_begin_ns\":%" PRIu64
           ",\"release_start_ns\":%" PRIu64 ",\"release_end_ns\":%" PRIu64
           ",\"snapshot_after_begin_ns\":%" PRIu64 ",\"snapshot_after_end_ns\":%" PRIu64 ",\"threads\":[",
           begin, end, snapshot_begin, release_start, release_end, snapshot_after_begin, snapshot_after_end);
    for (unsigned i = 0; i < n; i++) {
        struct worker *w = &workers[i];
        printf("%s{\"id\":%u,\"tid\":%ld,\"steps\":%" PRIu64 ",\"cpu_s\":%.9f,\"checksum\":\"%016" PRIx64
               "\",\"finished_ns\":%" PRIu64 ",\"max_postrelease_sample_gap_ns\":%" PRIu64
               ",\"release_start_ns\":%" PRIu64 ",\"release_end_ns\":%" PRIu64 ",\"events\":[",
               i ? "," : "", i, w->tid, w->steps, w->cpu_s, w->checksum, w->finished_ns, w->max_gap_ns,
               release_times[i][0], release_times[i][1]);
        for (unsigned j = 0; j < w->count; j++) {
            struct event *e = &w->events[j];
            printf("%s{\"previous_ns\":%" PRIu64 ",\"ns\":%" PRIu64 ",\"cpu\":%d}",
                   j ? "," : "", e->previous_ns, e->ns, e->cpu);
        }
        printf("]}");
    }
    printf("]}\n");
    pthread_barrier_destroy(&ready); pthread_barrier_destroy(&start); pthread_barrier_destroy(&started);
    free(threads); free(workers);
    return 0;
}
