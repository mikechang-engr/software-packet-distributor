/*
 * software-packet-distributor
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Mike Chang
 * Author: Mike Chang <mikechang.engr@gmail.com>
 */
#include "core_worker.h"
#include "globals.h"
int worker_main(void *arg){ unsigned idx=(unsigned)(uintptr_t)arg; unsigned lcore=WORKERS[idx]; printf("[worker-%u] started", lcore); putchar('\n'); struct rte_ring *in=g_worker_rings[idx]; struct rte_ring *out=g_tx_rings[idx]; struct rte_mbuf *pkts[BURST]; while(!g_quit){ unsigned n=rte_ring_dequeue_burst(in,(void**)pkts,BURST,NULL); if(unlikely(n==0)){ rte_pause(); continue;} g_worker_rx[idx]+=n; unsigned sent=rte_ring_enqueue_burst(out,(void**)pkts,n,NULL); g_worker_tx[idx]+=sent; for(unsigned i=sent;i<n;i++){ rte_pktmbuf_free(pkts[i]); g_worker_drop[idx]++; } } return 0; }
int sink_main(void *arg){ (void)arg; puts("[sink] started"); struct rte_mbuf *pkts[256]; while(!g_quit){ for(unsigned q=0;q<NB_WORKERS;q++){ unsigned n=rte_ring_dequeue_burst(g_tx_rings[q],(void**)pkts,256,NULL); for(unsigned i=0;i<n;i++){ rte_pktmbuf_free(pkts[i]); } } rte_pause(); } return 0; }
