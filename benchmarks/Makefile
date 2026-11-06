CC = gcc
CFLAGS = -O2 -std=gnu11 -Wall -Wextra -Werror -pthread

lb_bench: src/lb_bench.c
	$(CC) $(CFLAGS) $< -o $@

lb_affinity_probe: src/lb_affinity_probe.c
	$(CC) $(CFLAGS) $< -o $@
