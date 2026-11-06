#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* User-space workload: no scheduler changes and no CPU affinity changes. */
struct stats {
    uint64_t run_ns, wait_ns, slices, migrations;
    uint64_t hot_rejects, forced_migrations, running_rejects, affinity_rejects;
    uint64_t wakeups, wake_migrations;
};
#define DELTA(w, field) ((w)->after.field - (w)->before.field)
struct worker {
    unsigned id;
    uint64_t steps, words, checksum;
    int bursty;
    long tid;
    double elapsed_s, cpu_s;
    struct stats before, after;
};
static pthread_barrier_t ready, start;

static void die(const char *message)
{
    fprintf(stderr, "%s: %s\n", message, strerror(errno));
    exit(EXIT_FAILURE);
}

static double seconds(clockid_t clock)
{
    struct timespec ts;
    if (clock_gettime(clock, &ts)) die("clock_gettime");
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static struct stats read_stats(long tid)
{
    struct stats s = {0};
    char file[128], line[512];
    FILE *fp;
    unsigned found = 0;
    struct field { const char *name; uint64_t *value; } fields[] = {
        {"se.nr_migrations", &s.migrations},
        {"se.statistics.nr_failed_migrations_hot", &s.hot_rejects},
        {"se.statistics.nr_forced_migrations", &s.forced_migrations},
        {"se.statistics.nr_failed_migrations_running", &s.running_rejects},
        {"se.statistics.nr_failed_migrations_affine", &s.affinity_rejects},
        {"se.statistics.nr_wakeups", &s.wakeups},
        {"se.statistics.nr_wakeups_migrate", &s.wake_migrations},
    };
    snprintf(file, sizeof(file), "/proc/self/task/%ld/schedstat", tid);
    fp = fopen(file, "r");
    if (!fp) die(file);
    if (fscanf(fp, "%" SCNu64 " %" SCNu64 " %" SCNu64,
               &s.run_ns, &s.wait_ns, &s.slices) != 3) {
        fprintf(stderr, "Cannot parse %s\n", file);
        exit(EXIT_FAILURE);
    }
    fclose(fp);
    snprintf(file, sizeof(file), "/proc/self/task/%ld/sched", tid);
    fp = fopen(file, "r");
    if (!fp) die(file);
    while (fgets(line, sizeof(line), fp)) {
        char key[128];
        uint64_t value;
        if (sscanf(line, " %127[^:]: %" SCNu64, key, &value) != 2) continue;
        size_t length = strlen(key);
        while (length && (key[length - 1] == ' ' || key[length - 1] == '\t'))
            key[--length] = '\0';
        for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
            if (!strcmp(key, fields[i].name)) {
                *fields[i].value = value;
                found |= 1U << i;
            }
        }
    }
    fclose(fp);
    if (found != 127) {
        for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
            if (!(found & (1U << i)))
                fprintf(stderr, "Missing scheduler counter: %s\n", fields[i].name);
        fprintf(stderr, "Check CONFIG_SCHED_DEBUG, CONFIG_SCHEDSTATS and kernel.sched_schedstats=1\n");
        exit(EXIT_FAILURE);
    }
    return s;
}

static void *work(void *data)
{
    struct worker *w = data;
    uint64_t *buffer = NULL;
    uint64_t state = UINT64_C(0x123456789abcdef) + w->id;
    uint64_t done = 0;
    double wall_start, cpu_start;
    int rc = posix_memalign((void **)&buffer, 64, w->words * sizeof(*buffer));
    if (rc) { errno = rc; die("posix_memalign"); }
    for (uint64_t i = 0; i < w->words; i++)
        buffer[i] = (i + 1) * UINT64_C(0x9e3779b97f4a7c15);
    w->tid = syscall(SYS_gettid);
    pthread_barrier_wait(&ready);
    pthread_barrier_wait(&start);
    w->before = read_stats(w->tid);
    cpu_start = seconds(CLOCK_THREAD_CPUTIME_ID);
    wall_start = seconds(CLOCK_MONOTONIC);
    while (done < w->steps) {
        uint64_t batch = w->steps - done;
        if (batch > 262144) batch = 262144;
        for (uint64_t i = 0; i < batch; i++) {
            uint64_t index = (state >> 17) & (w->words - 1);
            state = state * UINT64_C(2862933555777941757) + buffer[index] + 1;
            buffer[index] = state ^ (state >> 23);
        }
        done += batch;
        if (w->bursty && done < w->steps) {
            struct timespec pause = {0, 1000000L * (1 + (w->id + done / 262144) % 3)};
            while (nanosleep(&pause, &pause) && errno == EINTR) { }
        }
    }
    w->elapsed_s = seconds(CLOCK_MONOTONIC) - wall_start;
    w->cpu_s = seconds(CLOCK_THREAD_CPUTIME_ID) - cpu_start;
    w->after = read_stats(w->tid);
    w->checksum = state ^ buffer[0] ^ buffer[w->words - 1];
    free(buffer);
    return NULL;
}

static uint64_t number(const char *text, const char *name)
{
    char *end;
    errno = 0;
    uint64_t value = strtoull(text, &end, 10);
    if (errno || !*text || *text == '-' || *end) {
        fprintf(stderr, "Invalid %s: %s\n", name, text);
        exit(EXIT_FAILURE);
    }
    return value;
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "Usage: %s WORKERS TOTAL_STEPS BUFFER_KIB steady|bursty\n", argv[0]);
        return EXIT_FAILURE;
    }
    uint64_t n = number(argv[1], "workers"), total = number(argv[2], "steps");
    uint64_t kib = number(argv[3], "buffer KiB");
    if (n < 1 || n > 64 || total < n || kib < 4 || kib > 16384 || (kib & (kib - 1)) ||
        (strcmp(argv[4], "steady") && strcmp(argv[4], "bursty"))) {
        fprintf(stderr, "workers=1..64; steps>=workers; buffer=power of 2, 4..16384 KiB\n");
        return EXIT_FAILURE;
    }
    struct worker *workers = calloc(n, sizeof(*workers));
    pthread_t *threads = calloc(n, sizeof(*threads));
    if (!workers || !threads) die("calloc");
    int rc = pthread_barrier_init(&ready, NULL, (unsigned)n + 1);
    if (!rc) rc = pthread_barrier_init(&start, NULL, (unsigned)n + 1);
    if (rc) { errno = rc; die("pthread_barrier_init"); }
    for (unsigned i = 0; i < n; i++) {
        workers[i].id = i;
        workers[i].steps = total / n + (i < total % n);
        workers[i].words = kib * 1024 / sizeof(uint64_t);
        workers[i].bursty = !strcmp(argv[4], "bursty");
        rc = pthread_create(&threads[i], NULL, work, &workers[i]);
        if (rc) { errno = rc; die("pthread_create"); }
    }
    pthread_barrier_wait(&ready);
    double begin = seconds(CLOCK_MONOTONIC);
    pthread_barrier_wait(&start);
    for (unsigned i = 0; i < n; i++) {
        rc = pthread_join(threads[i], NULL);
        if (rc) { errno = rc; die("pthread_join"); }
    }
    double elapsed = seconds(CLOCK_MONOTONIC) - begin;
    printf("{\"schema_version\":2,\"workers\":%" PRIu64 ",\"total_steps\":%" PRIu64
           ",\"buffer_kib\":%" PRIu64 ",\"mode\":\"%s\",\"elapsed_s\":%.9f,\"threads\":[",
           n, total, kib, argv[4], elapsed);
    for (unsigned i = 0; i < n; i++) {
        struct worker *w = &workers[i];
        printf("%s{\"id\":%u,\"tid\":%ld,\"steps\":%" PRIu64
               ",\"elapsed_s\":%.9f,\"cpu_s\":%.9f,\"wait_ns\":%" PRIu64
               ",\"migrations\":%" PRIu64 ",\"slices\":%" PRIu64
               ",\"hot_rejects\":%" PRIu64 ",\"forced_migrations\":%" PRIu64
               ",\"running_rejects\":%" PRIu64 ",\"affinity_rejects\":%" PRIu64
               ",\"wakeups\":%" PRIu64 ",\"wake_migrations\":%" PRIu64
               ",\"checksum\":\"%016" PRIx64 "\"}",
               i ? "," : "", i, w->tid, w->steps, w->elapsed_s, w->cpu_s,
               DELTA(w, wait_ns), DELTA(w, migrations), DELTA(w, slices),
               DELTA(w, hot_rejects), DELTA(w, forced_migrations),
               DELTA(w, running_rejects), DELTA(w, affinity_rejects),
               DELTA(w, wakeups), DELTA(w, wake_migrations), w->checksum);
    }
    puts("]}");
    pthread_barrier_destroy(&start);
    pthread_barrier_destroy(&ready);
    free(threads);
    free(workers);
    return 0;
}
