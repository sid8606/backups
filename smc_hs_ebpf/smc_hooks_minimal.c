#include <stdio.h>
#include <unistd.h>
#include "smc_hooks_minimal.skel.h"

int main(void)
{
	struct smc_hooks_minimal *skel;
	int err;

	skel = smc_hooks_minimal__open_and_load();
	if (!skel) {
		fprintf(stderr, "failed to open/load BPF program\n");
		return 1;
	}

	err = smc_hooks_minimal__attach(skel);
	if (err) {
		fprintf(stderr, "failed to attach struct_ops: %d\n", err);
		smc_hooks_minimal__destroy(skel);
		return 1;
	}

	printf("Minimal SMC eBPF hooks attached.\n");
	printf("Check /sys/kernel/debug/tracing/trace_pipe for logs.\n");

	while (1)
		sleep(5);

	smc_hooks_minimal__destroy(skel);
	return 0;
}

