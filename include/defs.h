/*
 * software-packet-distributor
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Mike Chang
 * Author: Mike Chang <mikechang.engr@gmail.com>
 */
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <rte_common.h>
#include <rte_branch_prediction.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ring.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_byteorder.h>
#include <rte_errno.h>
#include <rte_malloc.h>
#define NFLOWS 1024u
#define WHEEL_SLOTS 1024u
#define ELEPHANT_FLOWS 3u
#define RETA_SZ 256u
#define RETA_MASK (RETA_SZ - 1u)
#define FLOW_SET_SIZE 4096u
#define RING_SIZE 8192u
#define PIPE_SIZE 65536u
#define MBUF_DATAROOM 2176
#define POOL_CACHE 256
#define BURST 128
#define WIRE_BYTES 64
#define NB_WORKERS 8u
#define PERF_LOG(fmt, ...) do { printf(fmt, ##__VA_ARGS__); putchar('\n'); } while(0)
_Static_assert(RETA_SZ == 256u, "RETA_SZ must be 256");
_Static_assert((RETA_SZ & (RETA_SZ - 1u)) == 0u, "RETA_SZ must be pow2");
_Static_assert(NB_WORKERS == 8u, "Expect 8 workers (8..15)");
_Static_assert((FLOW_SET_SIZE & (FLOW_SET_SIZE - 1u)) == 0u, "Flow set size must be pow2");
