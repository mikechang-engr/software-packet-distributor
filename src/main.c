/*
 * software-packet-distributor
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Mike Chang
 * Author: Mike Chang <mikechang.engr@gmail.com>
 */
#include "globals.h"
#include "core_worker.h"
#include "core_distributor.h"
#include "core_generator.h"
#include "perf.h"
#include "flow.h"
static void on_signal(int sig){ (void)sig; g_quit = 1; rte_smp_wmb(); }
int main(int argc, char **argv){ signal(SIGINT, on_signal); signal(SIGTERM, on_signal); int ret=rte_eal_init(argc, argv); if(ret<0) rte_exit(EXIT_FAILURE, "EAL init failed"); setvbuf(stdout, NULL, _IOLBF, 0); build_flows_and_wheel(); build_header_templates(); build_reta(); banner(); if(!rte_lcore_is_enabled(PERF_CORE) || !rte_lcore_is_enabled(DISTA_CORE) || !rte_lcore_is_enabled(DISTB_CORE) || !rte_lcore_is_enabled(GEN_CORE) || !rte_lcore_is_enabled(SINK_CORE)) rte_exit(EXIT_FAILURE, "Perf/Distributor-A/Distributor-B/generator/sink core not enabled (-l)." ); for(unsigned i=0;i<NB_WORKERS;i++){ if(!rte_lcore_is_enabled(WORKERS[i])) rte_exit(EXIT_FAILURE, "Worker core %u not enabled (-l).", WORKERS[i]); } create_mempools(); create_rings(); create_fat(); sanity_check(); for(unsigned i=0;i<NB_WORKERS;i++){ rte_eal_remote_launch(worker_main, (void*)(uintptr_t)i, WORKERS[i]); } rte_eal_remote_launch(distB_main, NULL, DISTB_CORE); rte_eal_remote_launch(distA_main, NULL, DISTA_CORE); rte_eal_remote_launch(perf_main, NULL, PERF_CORE); rte_eal_remote_launch(gen_main, NULL, GEN_CORE); rte_eal_remote_launch(sink_main, NULL, SINK_CORE); rte_eal_mp_wait_lcore(); rte_eal_cleanup(); return 0; }
