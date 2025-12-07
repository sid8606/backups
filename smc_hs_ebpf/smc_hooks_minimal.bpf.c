// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

/* Minimal example struct_ops hooks for SMC handshake */

SEC("struct_ops/smc_syn_option")
int smc_syn_option(const struct tcp_sock *tp)
{
    bpf_printk("SMC SYN option hook called\n");
    return 0;  // allow
}

SEC("struct_ops/smc_synack_option")
int smc_synack_option(const struct tcp_sock *tp,
                      struct inet_request_sock *ireq)
{
    bpf_printk("SMC SYNACK option hook called\n");
    return 0;  // allow
}

/* Register the ops struct */
SEC(".struct_ops.link")
struct smc_hs_ctrl smc_hooks = {
    .name          = "smc_hooks",
    .syn_option    = (void *)smc_syn_option,
    .synack_option = (void *)smc_synack_option,
};

