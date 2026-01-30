/*
 * software-packet-distributor
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Mike Chang
 * Author: Mike Chang <mikechang.engr@gmail.com>
 */
#include "globals.h"
#include "flow.h"
#include "fat.h"
const unsigned PERF_CORE=5, DISTA_CORE=6, DISTB_CORE=7, GEN_CORE=4, SINK_CORE=3;
const unsigned WORKERS[NB_WORKERS] = {8,9,10,11,12,13,14,15};
volatile sig_atomic_t g_quit = 0;
struct rte_mempool *g_mempool_unused; /* placeholder to avoid warnings */
struct rte_mempool *g_mpool=NULL, *g_pipe_pool=NULL;
struct rte_ring *g_ingress_ring=NULL, *g_dist_pipe=NULL, *g_worker_rings[16]={0}, *g_tx_rings[16]={0};
volatile uint64_t g_gen_tx=0, g_gen_drop=0, g_dist_rx=0, g_dist_tx=0, g_dist_drop=0;
volatile uint64_t g_worker_rx[16]={0}, g_worker_tx[16]={0}, g_worker_drop[16]={0};
volatile uint64_t g_fat_hits=0, g_fat_misses=0, g_fat_evictions=0;
volatile uint32_t g_flow_count[16]={0}, g_flow_count_shadow[16]={0}, g_epoch=1u;
uint8_t g_reta[RETA_SZ]; uint64_t *g_fat=NULL;
static inline uint32_t lcg32_local(uint32_t *ps){ *ps = (*ps)*1664525u + 1013904223u; return *ps; }
static inline void ensure_dir(const char *path){ struct stat st; if (stat(path,&st)==0) return; (void)mkdir(path,0755); }
void create_mempools(void){ unsigned nb_mbufs=8192u + NB_WORKERS*4096u; g_mpool=rte_pktmbuf_pool_create("mp", nb_mbufs, POOL_CACHE, 0, MBUF_DATAROOM, rte_socket_id()); if(!g_mpool) rte_exit(EXIT_FAILURE, "mempool (mbuf) create failed: %s", rte_strerror(rte_errno)); g_pipe_pool=rte_mempool_create("pipe_pool", 65536u, 32, POOL_CACHE, 0, NULL,NULL,NULL,NULL, rte_socket_id(), 0); if(!g_pipe_pool) rte_exit(EXIT_FAILURE, "mempool (pipe) create failed: %s", rte_strerror(rte_errno)); }
void create_rings(void){ char rpfx[16]; snprintf(rpfx,sizeof(rpfx), "%d", getpid()); char name[64]; snprintf(name,sizeof(name), "RQ_INGRESS_%s", rpfx); g_ingress_ring=rte_ring_create(name, RING_SIZE, rte_socket_id(), RING_F_SP_ENQ|RING_F_SC_DEQ); if(!g_ingress_ring) rte_exit(EXIT_FAILURE, "ingress ring create failed: %s", rte_strerror(rte_errno)); snprintf(name,sizeof(name), "RQ_DIST_PIPE_%s", rpfx); g_dist_pipe=rte_ring_create(name, PIPE_SIZE, rte_socket_id(), RING_F_SP_ENQ|RING_F_SC_DEQ); if(!g_dist_pipe) rte_exit(EXIT_FAILURE, "dist pipe create failed: %s", rte_strerror(rte_errno)); for(unsigned i=0;i<NB_WORKERS;i++){ snprintf(name,sizeof(name), "RQ_WR_%u_%s", WORKERS[i], rpfx); g_worker_rings[i]=rte_ring_create(name, RING_SIZE, rte_socket_id(), RING_F_SP_ENQ|RING_F_SC_DEQ); if(!g_worker_rings[i]) rte_exit(EXIT_FAILURE, "worker ring create failed: %s", rte_strerror(rte_errno)); snprintf(name,sizeof(name), "RQ_TX_%u_%s", WORKERS[i], rpfx); g_tx_rings[i]=rte_ring_create(name, RING_SIZE, rte_socket_id(), RING_F_SP_ENQ|RING_F_SC_DEQ); if(!g_tx_rings[i]) rte_exit(EXIT_FAILURE, "tx ring create failed: %s", rte_strerror(rte_errno)); } }
void build_reta(void){ for(unsigned i=0,w=0,c=0;i<RETA_SZ;i++){ g_reta[i]=w; if(++c==32u){c=0; if(++w==NB_WORKERS) w=0;} } uint32_t s=0xC0FFEE11u; for(int i=(int)RETA_SZ-1;i>0;--i){ int j=(int)(lcg32_local(&s) % (uint32_t)(i+1)); uint8_t t=g_reta[i]; g_reta[i]=g_reta[j]; g_reta[j]=t; } }
void create_fat(void){ g_fat=(uint64_t*)rte_zmalloc_socket("fat", FAT_SIZE*sizeof(uint64_t), RTE_CACHE_LINE_SIZE, rte_socket_id()); if(!g_fat) rte_exit(EXIT_FAILURE, "FAT allocate failed: %s", rte_strerror(rte_errno)); }
void banner(void){ time_t t=time(NULL); struct tm lt; localtime_r(&t,&lt); char ts[64]; strftime(ts,sizeof(ts), "%Y-%m-%d %H:%M:%S %Z", &lt); puts("[software-packet-distributor] XXH distributor (v1.9.7)"); printf(" time : %s", ts); putchar('\n'); printf(" generator core : %u", GEN_CORE); putchar('\n'); printf(" Distributor-A core : %u", DISTA_CORE); putchar('\n'); printf(" Distributor-B core : %u", DISTB_CORE); putchar('\n'); printf(" sink core : %u", SINK_CORE); putchar('\n'); printf(" perf core : %u", PERF_CORE); putchar('\n'); printf(" workers : "); for(unsigned i=0;i<NB_WORKERS;i++){ printf("%u%s", WORKERS[i], (i+1<NB_WORKERS)?",":""); } putchar('\n'); printf(" ring size : %u", RING_SIZE); putchar('\n'); printf(" pipeline size : %u", PIPE_SIZE); putchar('\n'); printf(" flows : %u (mice+elephants; power-of-two)", NFLOWS); putchar('\n'); puts("[config] elephants: ON (3 flows ~10% each)"); puts(" UDP/TCP: ~50/50 via wheel (1024 slots; shuffled; elephants weighted if ON)"); puts(" worker select: FAT hit -> worker ; miss -> RETA[XXH32(MSB-8) & mask]"); puts(" FAT: 2048 entries (8B each), 8-probe window, 5-bit modular age"); }
void sanity_check(void){ unsigned counts[16]={0}; for(unsigned i=0;i<RETA_SZ;++i) counts[g_reta[i]]++; for(unsigned w=0; w<NB_WORKERS; ++w){ if(counts[w]==0){ printf("[sanity] RETA worker %u has 0 entries", w); putchar('\n'); } } if(rte_get_tsc_hz()==0){ puts("[sanity] invalid TSC hz (0)"); } if(!g_fat){ puts("[sanity] FAT not allocated"); } }
