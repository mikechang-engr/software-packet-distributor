/*
 * software-packet-distributor
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Mike Chang
 * Author: Mike Chang <mikechang.engr@gmail.com>
 */
#pragma once
#include "defs.h"
extern const unsigned PERF_CORE, DISTA_CORE, DISTB_CORE, GEN_CORE, SINK_CORE;
extern const unsigned WORKERS[NB_WORKERS];
extern volatile sig_atomic_t g_quit;
extern struct rte_mempool *g_mpool, *g_pipe_pool;
extern struct rte_ring *g_ingress_ring, *g_dist_pipe, *g_worker_rings[16], *g_tx_rings[16];
extern volatile uint64_t g_gen_tx, g_gen_drop, g_dist_rx, g_dist_tx, g_dist_drop;
extern volatile uint64_t g_worker_rx[16], g_worker_tx[16], g_worker_drop[16];
extern volatile uint64_t g_fat_hits, g_fat_misses, g_fat_evictions;
extern volatile uint32_t g_flow_count[16], g_flow_count_shadow[16], g_epoch;
extern uint8_t g_reta[RETA_SZ];
extern uint64_t *g_fat;
void create_mempools(void); void create_rings(void); void create_fat(void);
void build_reta(void); void banner(void); void sanity_check(void);
