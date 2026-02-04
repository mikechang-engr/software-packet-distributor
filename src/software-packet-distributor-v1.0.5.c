/*
 * software-packet-distributor
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Mike Chang
 * Author: Mike Chang &lt;mikechang.engr@gmail.com&gt;
 */

/*
 * Table of Contents
 *  1. Includes
 *  2. Macros & Constants
 *  3. Types & Globals
 *  4. Helpers (inline / hashing)
 *  5. Flow Wheel & Templates
 *  6. FAT & RETA
 *  7. CSV & I/O Helpers
 *  8. Pipeline: Generator / Dist-A / Dist-B / Worker / Sink
 *  9. Greedy Reshaper & Perf Telemetry
 * 10. Sanity & Resource Init
 * 11. main()
 * 12. Revision History
 */


/*
 * software-rss v1.9.7 — XXH32/XXH64 distributor with 8-byte FAT (56+3+5)
 * Date: 2026-01-05 01:27:14 GMT+08:00 (Taipei, GMT+08:00)
 *
 * Style: newline-safe logging (no "\n" in printf); use puts_nl()/putchar(10).
 */
/* ===============================
 * 1. Includes
 * =============================== */
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
static const unsigned PERF_CORE = 5;
static const unsigned DISTA_CORE = 6;
static const unsigned DISTB_CORE = 7;
static const unsigned GEN_CORE = 4;
static const unsigned SINK_CORE = 3;
static const unsigned WORKERS[] = {8,9,10,11,12,13,14,15};
static const unsigned NB_WORKERS = (unsigned)(sizeof(WORKERS)/sizeof(WORKERS[0]));
static const unsigned RING_SIZE = 8192;
static const unsigned PIPE_SIZE = 65536;
static const unsigned MBUF_DATAROOM = 2176;
static const unsigned POOL_CACHE = 256;
static const unsigned BURST = 128;
static const uint16_t WIRE_BYTES = 64;
static const double TARGET_GBPS = 2.5; /* fallback target if no env provided */
#define NFLOWS 1024u
#define WHEEL_SLOTS 1024u
#define ELEPHANT_FLOWS 3u
#define RETA_SZ 256u
#define RETA_MASK (RETA_SZ - 1u)
static struct rte_mempool *g_mpool;
static struct rte_mempool *g_pipe_pool;
static struct rte_ring *g_ingress_ring;
static struct rte_ring *g_dist_pipe;
static struct rte_ring *g_worker_rings[16] __rte_cache_aligned;
static struct rte_ring *g_tx_rings[16] __rte_cache_aligned;
static volatile uint64_t g_gen_tx __rte_cache_aligned = 0;
static volatile uint64_t g_gen_drop __rte_cache_aligned = 0;
static volatile uint64_t g_dist_rx __rte_cache_aligned = 0;
static volatile uint64_t g_dist_tx __rte_cache_aligned = 0;
static volatile uint64_t g_dist_drop __rte_cache_aligned = 0;
static volatile uint64_t g_worker_rx[16] __rte_cache_aligned = {0};
static volatile uint64_t g_worker_tx[16] __rte_cache_aligned = {0};
static volatile uint64_t g_worker_drop[16] __rte_cache_aligned = {0};
static volatile uint64_t g_fat_hits __rte_cache_aligned = 0;
static volatile uint64_t g_fat_misses __rte_cache_aligned = 0;
static volatile uint64_t g_fat_evictions __rte_cache_aligned = 0;
#define FLOW_SET_SIZE 4096u
static uint32_t g_flow_set[16][FLOW_SET_SIZE] __rte_cache_aligned;
static uint32_t g_flow_seen_epoch[16][FLOW_SET_SIZE] __rte_cache_aligned;
static volatile uint32_t g_flow_count[16] __rte_cache_aligned = {0};
static volatile uint32_t g_flow_count_shadow[16] __rte_cache_aligned = {0};
static volatile uint32_t g_epoch __rte_cache_aligned = 1u;
static uint8_t g_reta[RETA_SZ] __rte_cache_aligned;
static volatile sig_atomic_t g_quit = 0;
static void on_signal(int sig){ (void)sig; g_quit = 1; }
#define PERF_LOG(fmt, ...) do { printf(fmt, ##__VA_ARGS__); putchar(10); } while(0)

static inline void puts_nl(const char *s) { fputs(s, stdout); fputc(10, stdout); }
_Static_assert(RETA_SZ == 256u, "RETA_SZ must be 256");
_Static_assert((RETA_SZ & (RETA_SZ - 1u)) == 0u, "RETA_SZ must be pow2");
_Static_assert((sizeof(WORKERS)/sizeof(WORKERS[0])) == 8u, "Expect 8 workers (8..15)");
_Static_assert((FLOW_SET_SIZE & (FLOW_SET_SIZE - 1u)) == 0u, "Flow set size must be pow2");
static inline uint32_t rotl32(uint32_t x, int r){ return (x<<r) | (x>>(32-r)); }
static inline uint64_t rotl64(uint64_t x, int r){ return (x<<r) | (x>>(64-r)); }
#define XXH_PRIME32_1 0x9E3779B1u
#define XXH_PRIME32_2 0x85EBCA77u
#define XXH_PRIME32_3 0xC2B2AE3Du
#define XXH_PRIME32_4 0x27D4EB2Fu
#define XXH_PRIME32_5 0x165667B1u
static uint32_t xxh32(const void* input, size_t len, uint32_t seed)
{
  const uint8_t* p = (const uint8_t*)input; const uint8_t* bEnd = p + len; uint32_t h32;
  if (len >= 16) {
    uint32_t v1 = seed + XXH_PRIME32_1 + XXH_PRIME32_2;
    uint32_t v2 = seed + XXH_PRIME32_2; uint32_t v3 = seed + 0; uint32_t v4 = seed - XXH_PRIME32_1;
    const uint8_t* limit = bEnd - 16;
    do {
      uint32_t w1, w2, w3, w4;
      memcpy(&w1, p, 4); p+=4; v1 += w1 * XXH_PRIME32_2; v1 = rotl32(v1,13); v1 *= XXH_PRIME32_1;
      memcpy(&w2, p, 4); p+=4; v2 += w2 * XXH_PRIME32_2; v2 = rotl32(v2,13); v2 *= XXH_PRIME32_1;
      memcpy(&w3, p, 4); p+=4; v3 += w3 * XXH_PRIME32_2; v3 = rotl32(v3,13); v3 *= XXH_PRIME32_1;
      memcpy(&w4, p, 4); p+=4; v4 += w4 * XXH_PRIME32_2; v4 = rotl32(v4,13); v4 *= XXH_PRIME32_1;
    } while (p <= limit);
    h32 = rotl32(v1,1) + rotl32(v2,7) + rotl32(v3,12) + rotl32(v4,18);
  } else { h32 = seed + XXH_PRIME32_5; }
  h32 += (uint32_t)len;
  while ((p+4) <= bEnd) { uint32_t k1; memcpy(&k1,p,4); p+=4; h32 += k1 * XXH_PRIME32_3; h32 = rotl32(h32,17) * XXH_PRIME32_4; }
  while (p < bEnd) { h32 += (*p) * XXH_PRIME32_5; p++; h32 = rotl32(h32,11) * XXH_PRIME32_1; }
  h32 ^= h32 >> 15; h32 *= XXH_PRIME32_2; h32 ^= h32 >> 13; h32 *= XXH_PRIME32_3; h32 ^= h32 >> 16; return h32;
}
#define XXH_PRIME64_1 0x9E3779B185EBCA87ULL
#define XXH_PRIME64_2 0xC2B2AE3D27D4EB4FULL
#define XXH_PRIME64_3 0x165667B19E3779F9ULL
#define XXH_PRIME64_4 0x85EBCA77C2B2AE63ULL
#define XXH_PRIME64_5 0x27D4EB2F165667C5ULL
static uint64_t xxh64(const void* input, size_t len, uint64_t seed)
{
  const uint8_t* p = (const uint8_t*)input; const uint8_t* bEnd = p + len; uint64_t h64;
  if (len >= 32) {
    uint64_t v1 = seed + XXH_PRIME64_1 + XXH_PRIME64_2;
    uint64_t v2 = seed + XXH_PRIME64_2; uint64_t v3 = seed + 0; uint64_t v4 = seed - XXH_PRIME64_1;
    const uint8_t* limit = bEnd - 32;
    do {
      uint64_t w1,w2,w3,w4;
      memcpy(&w1,p,8); p+=8; v1 += w1 * XXH_PRIME64_2; v1 = rotl64(v1,31); v1 *= XXH_PRIME64_1;
      memcpy(&w2,p,8); p+=8; v2 += w2 * XXH_PRIME64_2; v2 = rotl64(v2,31); v2 *= XXH_PRIME64_1;
      memcpy(&w3,p,8); p+=8; v3 += w3 * XXH_PRIME64_2; v3 = rotl64(v3,31); v3 *= XXH_PRIME64_1;
      memcpy(&w4,p,8); p+=8; v4 += w4 * XXH_PRIME64_2; v4 = rotl64(v4,31); v4 *= XXH_PRIME64_1;
    } while (p <= limit);
    h64 = rotl64(v1,1) + rotl64(v2,7) + rotl64(v3,12) + rotl64(v4,18);
  } else { h64 = seed + XXH_PRIME64_5; }
  h64 += (uint64_t)len;
  while ((p+8) <= bEnd) { uint64_t k1; memcpy(&k1,p,8); p+=8; k1 *= XXH_PRIME64_2; k1 = rotl64(k1,31); k1 *= XXH_PRIME64_1; h64 ^= k1; h64 = rotl64(h64,27) * XXH_PRIME64_1 + XXH_PRIME64_4; }
  while (p < bEnd) { h64 ^= (*p) * XXH_PRIME64_5; p++; h64 = rotl64(h64,11) * XXH_PRIME64_1; }
  h64 ^= h64 >> 33; h64 *= XXH_PRIME64_2; h64 ^= h64 >> 29; h64 *= XXH_PRIME64_3; h64 ^= h64 >> 32; return h64;
}
static const uint32_t XXH32_SEED = 0x9E3779B1u;
static const uint64_t XXH64_SEED = 0x9E3779B97F4A7C15ULL;
struct dist_item { struct rte_mbuf *m; uint16_t wi; uint32_t flow_sig; };
static inline __rte_always_inline void track_flow(unsigned wi, uint32_t sig)
{
  const uint32_t mask = FLOW_SET_SIZE - 1u; uint32_t idx = sig & mask;
  for (unsigned probe = 0; probe < 8u; ++probe) {
    if (g_flow_seen_epoch[wi][idx] != g_epoch) { g_flow_seen_epoch[wi][idx] = g_epoch; g_flow_set[wi][idx] = sig; g_flow_count[wi]++; return; }
    if (g_flow_set[wi][idx] == sig) { return; }
    idx = (idx + 1u) & mask;
  }
}
enum proto_e { PROTO_UDP = 17, PROTO_TCP = 6 };
typedef struct Flow { uint8_t src_ip[4], dst_ip[4]; enum proto_e proto; uint16_t sport_base, dport_base; } Flow;
static Flow g_flows[NFLOWS];
static uint32_t g_wheel[WHEEL_SLOTS] __rte_cache_aligned; static uint32_t g_wheel_pos __rte_cache_aligned = 0u;
static inline __rte_always_inline uint32_t lcg32(uint32_t *s){ *s = (*s) * 1664525u + 1013904223u; return *s; }
static uint32_t prng_s = 0xC0FFEE11u; static inline __rte_always_inline uint32_t prng(void){ return lcg32(&prng_s); }
static uint8_t l2_ip_udp_tmpl[14 + 20 + 8]; static uint8_t l2_ip_tcp_tmpl[14 + 20 + 20];
static void build_header_templates(void)
{
  memset(l2_ip_udp_tmpl, 0, sizeof(l2_ip_udp_tmpl)); memset(l2_ip_tcp_tmpl, 0, sizeof(l2_ip_tcp_tmpl));
  l2_ip_udp_tmpl[12] = 0x08; l2_ip_udp_tmpl[13] = 0x00; l2_ip_tcp_tmpl[12] = 0x08; l2_ip_tcp_tmpl[13] = 0x00;
  l2_ip_udp_tmpl[14] = 0x45; l2_ip_udp_tmpl[22] = 64; l2_ip_tcp_tmpl[14] = 0x45; l2_ip_tcp_tmpl[22] = 64;
  l2_ip_udp_tmpl[23] = PROTO_UDP; l2_ip_tcp_tmpl[23] = PROTO_TCP;
  uint16_t iplen_udp = (uint16_t)(20 + 8 + (WIRE_BYTES - (14 + 20 + 8)));
  uint16_t iplen_tcp = (uint16_t)(20 + 20 + (WIRE_BYTES - (14 + 20 + 20)));
  l2_ip_udp_tmpl[16] = (uint8_t)(iplen_udp >> 8); l2_ip_udp_tmpl[17] = (uint8_t)(iplen_udp);
  l2_ip_tcp_tmpl[16] = (uint8_t)(iplen_tcp >> 8); l2_ip_tcp_tmpl[17] = (uint8_t)(iplen_tcp);
  l2_ip_tcp_tmpl[34] = (5u << 4);
}
#define FAT_SIZE 2048u
static uint64_t *g_fat = NULL; static inline __rte_always_inline uint64_t fat_get(uint32_t idx){ return g_fat[idx]; }
static inline __rte_always_inline uint64_t fat_fp56(uint64_t u){ return u >> 8; }
static inline __rte_always_inline uint8_t  fat_W3 (uint64_t u){ return (uint8_t)((u >> 5) & 0x07); }
static inline __rte_always_inline uint8_t  fat_A5 (uint64_t u){ return (uint8_t)(u & 0x1F); }
static inline __rte_always_inline uint64_t fat_pack(uint64_t fp56, uint8_t W3, uint8_t A5){ return (fp56 << 8) | (((uint64_t)W3 & 0x7) << 5) | ((uint64_t)A5 & 0x1F); }
static inline __rte_always_inline uint64_t fat_set_age(uint64_t u, uint8_t A5){ return (u & ~0x1FULL) | ((uint64_t)A5 & 0x1F); }
static inline __rte_always_inline int fat_lookup_tag(uint64_t fp56, uint64_t h64, uint16_t *out_wi)
{
  uint32_t mask = FAT_SIZE - 1u; uint32_t idx = (uint32_t)h64 & mask; rte_prefetch0(&g_fat[idx]);
  for (unsigned p = 0; p < 8u; ++p) {
    uint64_t u = fat_get(idx); if (u == 0) { break; }
    if (fat_fp56(u) == fp56) { *out_wi = (uint16_t)fat_W3(u); g_fat[idx] = fat_set_age(u, (uint8_t)(g_epoch & 31)); return 1; }
    idx = (idx + 1u) & mask;
  }
  return 0;
}
static inline __rte_always_inline void fat_insert_tag(uint64_t fp56, uint64_t h64, uint16_t wi)
{
  uint32_t mask = FAT_SIZE - 1u; uint32_t idx = (uint32_t)h64 & mask; int empty = -1; uint8_t nowA = (uint8_t)(g_epoch & 31);
  uint32_t oldest_idx = idx; uint8_t oldest_delta = 0;
  for (unsigned p = 0; p < 8u; ++p) {
    uint64_t u = fat_get(idx); if (u == 0) { empty = (int)idx; break; }
    uint8_t a = fat_A5(u); uint8_t delta = (uint8_t)((nowA - a) & 31);
    if (delta > oldest_delta) { oldest_delta = delta; oldest_idx = idx; }
    idx = (idx + 1u) & mask;
  }
  uint32_t tgt = (empty >= 0) ? (uint32_t)empty : oldest_idx; if (empty < 0) g_fat_evictions++;
  g_fat[tgt] = fat_pack(fp56, (uint8_t)wi, nowA);
}
/* ELEPHANTS runtime control via env ELEPHANTS=on|off (default: on) */
static inline bool elephants_enabled(void)
{
  const char *s = getenv("ELEPHANTS");
  if (!s) return true; /* default ON for backward-compat */
  return strcasecmp(s, "on") == 0;
}
/* GREEDY runtime control via env GREEDY=on|off (default: on) */
static inline bool greedy_enabled(void)
{
  const char *s = getenv("GREEDY");
  if (!s) return true; /* default ON for backward-compat */
  return strcasecmp(s, "on") == 0;
}
static void build_flows_and_wheel(void)
{
  for (unsigned i = 0; i < NFLOWS; ++i) {
    Flow *f = &g_flows[i]; uint32_t seed = 0xC001CAFEu ^ i;
    f->src_ip[0] = 192; f->src_ip[1] = 168; f->src_ip[2] = (uint8_t)(lcg32(&seed) & 0xFF); f->src_ip[3] = (uint8_t)(lcg32(&seed) & 0xFF);
    f->dst_ip[0] = 10;  f->dst_ip[1] = 0;   f->dst_ip[2] = (uint8_t)(lcg32(&seed) & 0xFF); f->dst_ip[3] = (uint8_t)(lcg32(&seed) & 0xFF);
    f->sport_base = (uint16_t)((10000u + i) & 0xFFFFu); f->dport_base = (uint16_t)((20000u + i) & 0xFFFFu);
    f->proto = (i % 2u) ? PROTO_TCP : PROTO_UDP;
  }
  unsigned pos = 0;
  if (elephants_enabled()) {
    unsigned eid[ELEPHANT_FLOWS] = { NFLOWS-3u, NFLOWS-2u, NFLOWS-1u };
    g_flows[eid[0]].proto = PROTO_UDP; g_flows[eid[1]].proto = PROTO_TCP; g_flows[eid[2]].proto = PROTO_TCP;
    unsigned ele_slots = (unsigned)(WHEEL_SLOTS * 0.10); if (ele_slots == 0u) ele_slots = 1u;
    for (unsigned e = 0; e < ELEPHANT_FLOWS; ++e)
      for (unsigned k = 0; k < ele_slots && pos < WHEEL_SLOTS; ++k) g_wheel[pos++] = eid[e];
  }
  for (unsigned i = 0; i < NFLOWS && pos < WHEEL_SLOTS; ++i) { g_wheel[pos++] = i; }
  for (unsigned i = 0; pos < WHEEL_SLOTS; ++i) { g_wheel[pos++] = (i % NFLOWS); }
  for (int i = (int)WHEEL_SLOTS - 1; i > 0; --i) { int j = (int)(prng() % (uint32_t)(i + 1)); uint32_t t = g_wheel[i]; g_wheel[i] = g_wheel[j]; g_wheel[j] = t; }
}
/* Banner */
static void banner(void)
{
  time_t t = time(NULL); struct tm lt; localtime_r(&t, &lt);
  char ts[64]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S %Z", &lt);
  puts_nl("[software-rss] XXH distributor (v1.9.7)");
  printf(" time : %s", ts); putchar('\n');
  printf(" generator core : %u", GEN_CORE); putchar('\n');
  printf(" Distributor-A core : %u", DISTA_CORE); putchar('\n');
  printf(" Distributor-B core : %u", DISTB_CORE); putchar('\n');
  printf(" sink core : %u", SINK_CORE); putchar('\n');
  printf(" perf core : %u", PERF_CORE); putchar('\n');
  printf(" workers : "); for (unsigned i = 0; i < NB_WORKERS; i++) { printf("%u%s", WORKERS[i], (i+1 < NB_WORKERS) ? "," : ""); } putchar('\n');
  printf(" ring size : %u", RING_SIZE); putchar('\n');
  printf(" pipeline size : %u", PIPE_SIZE); putchar('\n');
  printf(" flows : %u (mice+elephants; power-of-two)", NFLOWS); putchar('\n');
  puts_nl(elephants_enabled() ? "[config] elephants: ON (3 flows ~10% each)" : "[config] elephants: OFF (all 1024 are mice)");
  puts_nl(" UDP/TCP: ~50/50 via wheel (1024 slots; shuffled; elephants weighted if ON)");
  puts_nl(" worker select: FAT hit -> worker ; miss -> RETA[XXH32(MSB-8) & mask]");
  puts_nl(" FAT: 2048 entries (8B each), 8-probe window, 5-bit modular age");
}
/* CSV helpers */
static void ensure_dir(const char *path){ struct stat st; if (stat(path,&st) == 0) return; (void)mkdir(path,0755); }
static FILE* open_csv(const char *path)
{
  ensure_dir("/var/log/software-rss"); FILE *f = fopen(path, "a"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); long sz = ftell(f);
  if (sz <= 0) { fputs("epoch,worker,rx_kpps,tx_kpps,drops,flows,fat_hits,fat_misses,fat_evictions", f); fputc('\n', f); fflush(f); }
  return f;
}
/* Read target rate from environment; MPPS precedence over GBPS. Returns PPS. */
static inline double get_target_pps_from_env(void)
{
  const char *s_mpps = getenv("TARGET_MPPS");
  const char *s_gbps = getenv("TARGET_GBPS");
  if (s_mpps && s_mpps[0]) { char *end=NULL; double mpps=strtod(s_mpps,&end); if (end!=s_mpps && mpps>0.0) return mpps*1e6; }
  if (s_gbps && s_gbps[0]) { char *end=NULL; double gbps=strtod(s_gbps,&end); if (end!=s_gbps && gbps>0.0) return (gbps*1e9)/(WIRE_BYTES*8.0); }
  return (TARGET_GBPS*1e9)/(WIRE_BYTES*8.0);
}
/* Generator (core 4) */
static int gen_main(void *arg)
{
  (void)arg; puts_nl("[generator] started"); struct rte_mbuf *pkts[BURST];
  const uint64_t hz = rte_get_tsc_hz(); const double target_pps = get_target_pps_from_env();
  double bursts_per_sec = target_pps / (double)BURST; if (bursts_per_sec < 1.0) bursts_per_sec = 1.0;
  uint64_t cycles_per_burst = (uint64_t)((double)hz / bursts_per_sec); if (!cycles_per_burst) cycles_per_burst = 1;
  uint64_t next_deadline = rte_get_tsc_cycles(); bool ramp = true; uint64_t ramp_cycles = (uint64_t)(0.25 * (double)hz);
  while (!g_quit) {
    uint64_t now = rte_get_tsc_cycles(); if (now < next_deadline) { while (rte_get_tsc_cycles() < next_deadline) { if (g_quit) break; rte_pause(); } }
    next_deadline += cycles_per_burst; unsigned this_burst = ramp ? (BURST/2) : BURST; unsigned idx = 0;
    if (rte_pktmbuf_alloc_bulk(g_mpool, pkts, this_burst) == 0) { idx = this_burst; }
    else { for (; idx < this_burst; idx++) { struct rte_mbuf *m = rte_pktmbuf_alloc(g_mpool); if (!m) { break; } pkts[idx] = m; } }
    for (unsigned i = 0; i < idx; i++) {
      uint32_t fidx = g_wheel[g_wheel_pos]; g_wheel_pos = (g_wheel_pos + 1) & (WHEEL_SLOTS - 1);
      char *p_raw = (char*)rte_pktmbuf_append(pkts[i], WIRE_BYTES); if (!p_raw) { continue; }
      uint8_t *p = (uint8_t*)p_raw; const Flow *f = &g_flows[fidx]; const bool is_udp = (f->proto == PROTO_UDP);
      const uint8_t *tmpl = is_udp ? l2_ip_udp_tmpl : l2_ip_tcp_tmpl; unsigned hdrlen = is_udp ? (14+20+8) : (14+20+20);
      memcpy(p, tmpl, hdrlen); uint8_t *ip = p + 14; uint8_t *l4 = ip + 20;
      ip[12]=f->src_ip[0]; ip[13]=f->src_ip[1]; ip[14]=f->src_ip[2]; ip[15]=f->src_ip[3];
      ip[16]=f->dst_ip[0]; ip[17]=f->dst_ip[1]; ip[18]=f->dst_ip[2]; ip[19]=f->dst_ip[3];
      uint16_t sport_be = rte_cpu_to_be_16(f->sport_base); uint16_t dport_be = rte_cpu_to_be_16(f->dport_base);
      l4[0]=(uint8_t)(sport_be>>8); l4[1]=(uint8_t)(sport_be); l4[2]=(uint8_t)(dport_be>>8); l4[3]=(uint8_t)(dport_be);
    }
    if (idx) { unsigned n = rte_ring_enqueue_burst(g_ingress_ring, (void**)pkts, idx, NULL); g_gen_tx += n; if (n < idx) { g_gen_drop += (idx - n); for (unsigned i = n; i < idx; i++) { rte_pktmbuf_free(pkts[i]); } } }
    if (ramp) { if (ramp_cycles > cycles_per_burst) ramp_cycles -= cycles_per_burst; else ramp = false; }
  }
  return 0;
}
static inline uint16_t pick_worker(uint32_t h){ return (uint16_t)g_reta[h & RETA_MASK]; }
static int distA_main(void *arg)
{
  (void)arg; puts_nl("[Distributor-A] started (FAT: 8B, 56+3+5; XXH32/XXH64)"); struct rte_mbuf *rx[BURST]; struct dist_item *items[BURST];
  while (!g_quit) {
    unsigned n = rte_ring_dequeue_burst(g_ingress_ring, (void**)rx, BURST, NULL); if (unlikely(n == 0)) { rte_pause(); continue; }
    g_dist_rx += n; unsigned w = 0;
    for (unsigned i = 0; i < n; i++) {
      if (likely(i+1 < n)) { rte_prefetch0(rte_pktmbuf_mtod(rx[i+1], void*)); }
      rte_prefetch0(rte_pktmbuf_mtod(rx[i], void*));
      struct dist_item *di = NULL; if (unlikely(rte_mempool_get(g_pipe_pool, (void**)&di) != 0 || di == NULL)) { rte_pktmbuf_free(rx[i]); g_dist_drop++; items[i] = NULL; continue; }
      const uint8_t *p = rte_pktmbuf_mtod(rx[i], const uint8_t*); const uint8_t *ip = p + 14; const uint8_t *l4 = ip + 20;
      uint8_t tuple13[13];
      memcpy(tuple13, ip+12, 4); memcpy(tuple13+4, ip+16, 4); tuple13[8] = ip[9]; tuple13[9] = l4[0]; tuple13[10] = l4[1]; tuple13[11] = l4[2]; tuple13[12] = l4[3];
      uint64_t h64 = xxh64(tuple13, sizeof(tuple13), XXH64_SEED); uint32_t h32 = xxh32(tuple13, sizeof(tuple13), XXH32_SEED); uint64_t fp56 = h64 >> 8; uint16_t wi;
      if (fat_lookup_tag(fp56, h64, &wi)) { g_fat_hits++; }
      else { uint32_t reta_idx = (h32 >> 24) & 0xFF; wi = pick_worker(reta_idx); fat_insert_tag(fp56, h64, wi); g_fat_misses++; }
      di->m = rx[i]; di->wi = wi; di->flow_sig = h32; items[w++] = di;
    }
    if (w) {
      unsigned pushed = rte_ring_enqueue_burst(g_dist_pipe, (void**)items, w, NULL);
      if (unlikely(pushed < w)) { for (unsigned i = pushed; i < w; i++){ if (items[i]) { rte_pktmbuf_free(items[i]->m); rte_mempool_put(g_pipe_pool, items[i]); g_dist_drop++; } } }
    }
  }
  return 0;
}
static int distB_main(void *arg)
{
  (void)arg; puts_nl("[Distributor-B] started"); struct dist_item *items[BURST]; struct rte_mbuf *wk_pkts[16][BURST]; uint16_t wk_cnt[16];
  while (!g_quit) {
    unsigned n = rte_ring_dequeue_burst(g_dist_pipe, (void**)items, BURST, NULL); if (unlikely(n == 0)) { rte_pause(); continue; }
    for (unsigned wi = 0; wi < NB_WORKERS; wi++) { wk_cnt[wi] = 0; }
    for (unsigned i = 0; i < n; i++) { rte_prefetch0(items[i]); if (likely(i+1 < n)) { rte_prefetch0(items[i+1]); } }
    for (unsigned i = 0; i < n; i++) {
      struct dist_item *di = items[i]; if (unlikely(!di)) continue; unsigned wi = di->wi; struct rte_mbuf *m = di->m; uint32_t sig = di->flow_sig;
      if (unlikely(wi >= NB_WORKERS)) { rte_pktmbuf_free(m); rte_mempool_put(g_pipe_pool, di); g_dist_drop++; continue; }
      unsigned pos = wk_cnt[wi]; if (pos < BURST) { wk_pkts[wi][pos] = m; wk_cnt[wi] = (uint16_t)(pos + 1); track_flow(wi, sig); }
      else { unsigned sent = rte_ring_enqueue_burst(g_worker_rings[wi], (void**)wk_pkts[wi], pos, NULL); g_dist_tx += sent; for (unsigned j = sent; j < pos; j++){ rte_pktmbuf_free(wk_pkts[wi][j]); g_worker_drop[wi]++; g_dist_drop++; } wk_cnt[wi] = 0; wk_pkts[wi][wk_cnt[wi]++] = m; }
      rte_mempool_put(g_pipe_pool, di);
    }
    for (unsigned wi = 0; wi < NB_WORKERS; wi++) { unsigned cnt = wk_cnt[wi]; if (!cnt) continue; unsigned sent = rte_ring_enqueue_burst(g_worker_rings[wi], (void**)wk_pkts[wi], cnt, NULL); g_dist_tx += sent; for (unsigned j = sent; j < cnt; j++){ rte_pktmbuf_free(wk_pkts[wi][j]); g_worker_drop[wi]++; g_dist_drop++; } wk_cnt[wi] = 0; }
  }
  return 0;
}
static int worker_main(void *arg)
{
  unsigned idx = (unsigned)(uintptr_t)arg; unsigned lcore = WORKERS[idx]; printf("[worker-%u] started", lcore); putchar('\n'); struct rte_ring *in = g_worker_rings[idx]; struct rte_ring *out = g_tx_rings[idx]; struct rte_mbuf *pkts[BURST];
  while (!g_quit) {
    unsigned n = rte_ring_dequeue_burst(in, (void**)pkts, BURST, NULL); if (unlikely(n == 0)) { rte_pause(); continue; }
    g_worker_rx[idx] += n; unsigned sent = rte_ring_enqueue_burst(out, (void**)pkts, n, NULL); g_worker_tx[idx] += sent; for (unsigned i = sent; i < n; i++){ rte_pktmbuf_free(pkts[i]); g_worker_drop[idx]++; }
  }
  return 0;
}
static int sink_main(void *arg)
{
  (void)arg; puts_nl("[sink] started"); struct rte_mbuf *pkts[256];
  while (!g_quit) { for (unsigned q = 0; q < NB_WORKERS; q++) { unsigned n = rte_ring_dequeue_burst(g_tx_rings[q], (void**)pkts, 256, NULL); for (unsigned i = 0; i < n; i++){ rte_pktmbuf_free(pkts[i]); } } rte_pause(); }
  return 0;
}
static inline void reshuffle_wheel(void)
{
  for (int i = (int)WHEEL_SLOTS - 1; i > 0; --i) { int j = (int)(prng() % (uint32_t)(i + 1)); uint32_t t = g_wheel[i]; g_wheel[i] = g_wheel[j]; g_wheel[j] = t; }
}
static void mutate_flows_chunk(unsigned sec_idx, unsigned cycle_idx)
{
  unsigned start = sec_idx * 128u; unsigned end = start + 128u;
  static const uint8_t ip_inc2[4] = { 37, 73, 109, 181 };
  static const uint8_t ip_inc3[4] = { 41, 79, 127, 193 };
  static const uint8_t ip_incD2[4] = { 55, 95, 139, 203 };
  static const uint8_t ip_incD3[4] = { 61, 103, 149, 211 };
  static const uint16_t sport_inc[4]= { 131, 197, 263, 331 };
  static const uint16_t dport_inc[4]= { 149, 211, 277, 353 };
  uint8_t s2 = ip_inc2 [cycle_idx & 3u]; uint8_t s3 = ip_inc3 [cycle_idx & 3u]; uint8_t d2 = ip_incD2[cycle_idx & 3u]; uint8_t d3 = ip_incD3[cycle_idx & 3u]; uint16_t si = sport_inc[cycle_idx & 3u]; uint16_t di = dport_inc[cycle_idx & 3u];
  for (unsigned i = start; i < end; i++) { Flow *f = &g_flows[i]; f->src_ip[2] = (uint8_t)(f->src_ip[2] + s2); f->src_ip[3] = (uint8_t)(f->src_ip[3] + s3); f->dst_ip[2] = (uint8_t)(f->dst_ip[2] + d2); f->dst_ip[3] = (uint8_t)(f->dst_ip[3] + d3); f->sport_base = (uint16_t)(f->sport_base + si); f->dport_base = (uint16_t)(f->dport_base + di); if (((i - start + cycle_idx) & 3u) == 0u) { f->proto = (f->proto == PROTO_UDP) ? PROTO_TCP : PROTO_UDP; } }
  reshuffle_wheel();
}

/*
 * Greedy Shaper Overview
 * ----------------------
 * The greedy shaper periodically examines A72 worker cores to identify the
 * most heavily loaded worker and redistributes flows to achieve load balance.
 * From the pool of 256 flows, any flow currently mapped to an overloaded
 * worker may be reassigned to a less busy worker.
 *
 * The current implementation triggers a greedy-shaper cycle every 200 micro-
 * seconds. Under dual 25 GbE input with 64-byte packets, this interval may be
 * too coarse; a packet-based trigger (e.g., reshaping every ~1000 packets,
 * depending on DPIO buffer depth) may provide improved responsiveness.
 *
 * Static flow-to-worker binding is not currently supported and should be
 * considered in future algorithm design.
 */

/* Minimal greedy reshaper: move RETA entries from hottest to coldest worker. */
static unsigned greedy_reshaper_tick(const double *rx_vals, unsigned max_moves)
{
  if (!greedy_enabled()) return 0u;
  /* find hottest and coldest by current kpps snapshot */
  unsigned hot = 0, cold = 0; double hot_v = rx_vals[0], cold_v = rx_vals[0];
  for (unsigned wi = 1; wi < NB_WORKERS; wi++) {
    if (rx_vals[wi] > hot_v) { hot_v = rx_vals[wi]; hot = wi; }
    if (rx_vals[wi] < cold_v) { cold_v = rx_vals[wi]; cold = wi; }
  }
  if (hot == cold) return 0u;
  /* perform up to max_moves substitutions hot->cold in RETA */
  unsigned moves = 0; unsigned start = (unsigned)(prng() & RETA_MASK);
  for (unsigned i = 0; i < RETA_SZ && moves < max_moves; i++) {
    unsigned idx = (start + i) & RETA_MASK;
    if (g_reta[idx] == hot) { g_reta[idx] = (uint8_t)cold; moves++; }
  }
  return moves;
}
static int perf_main(void *arg)
{
  (void)arg; puts_nl("[perf] started"); const uint64_t hz = rte_get_tsc_hz(); uint64_t last_1s = rte_get_tsc_cycles(); uint64_t rx1[16] = {0}, tx1[16] = {0}, d1[16] = {0}; uint64_t gen_tx1=0, gen_dp1=0, dist_rx1=0, dist_tx1=0, dist_dp1=0; uint64_t fat_hit1=0, fat_mis1=0, fat_evc1=0;
  FILE *csv = open_csv("/var/log/software-rss/worker_stats_v197.csv"); unsigned seconds_seen = 0;
  while (!g_quit) {
    rte_delay_us_block(200000); uint64_t now = rte_get_tsc_cycles(); uint64_t delta = now - last_1s; if (delta < hz) continue; unsigned ticks = (unsigned)(delta / hz); double sec_1s = (double)ticks; last_1s += (uint64_t)ticks * hz;
    for (unsigned t = 0; t < ticks; ++t) { unsigned cur = seconds_seen + t + 1u; unsigned sec_idx = (cur - 1u) & 7u; unsigned cycle_idx = (cur - 1u) / 8u; mutate_flows_chunk(sec_idx, cycle_idx); }
    seconds_seen += ticks; time_t epoch = time(NULL);
    double wrx_sum=0, wtx_sum=0, wdp_sum=0; double rx_vals[16];
    for (unsigned wi = 0; wi < NB_WORKERS; wi++) {
      uint64_t rx_d = g_worker_rx[wi] - rx1[wi]; rx1[wi] = g_worker_rx[wi]; uint64_t tx_d = g_worker_tx[wi] - tx1[wi]; tx1[wi] = g_worker_tx[wi]; uint64_t dp_d = g_worker_drop[wi] - d1[wi]; d1[wi] = g_worker_drop[wi];
      double rx_kpps = (sec_1s>0? (double)rx_d/sec_1s:0)/1e3; double tx_kpps = (sec_1s>0? (double)tx_d/sec_1s:0)/1e3; double dp_kpps = (sec_1s>0? (double)dp_d/sec_1s:0)/1e3; wrx_sum += rx_kpps; wtx_sum += tx_kpps; wdp_sum += dp_kpps; rx_vals[wi] = rx_kpps;
      PERF_LOG("[perf] w%02u rx=%.2f Kpps tx=%.2f Kpps drop=%.2f Kpps flows=%u", WORKERS[wi], rx_kpps, tx_kpps, dp_kpps, (unsigned)g_flow_count_shadow[wi]);
      if (csv) { fprintf(csv, "%ld,%u,%.3f,%.3f,%.3f,%u,%llu,%llu,%llu", (long)epoch, WORKERS[wi], rx_kpps, tx_kpps, dp_kpps, (unsigned)g_flow_count_shadow[wi], (unsigned long long)(g_fat_hits - fat_hit1), (unsigned long long)(g_fat_misses - fat_mis1), (unsigned long long)(g_fat_evictions - fat_evc1)); fputc('\n', csv); }
    }
    uint64_t gtx_d = g_gen_tx - gen_tx1; gen_tx1 = g_gen_tx; uint64_t gdp_d = g_gen_drop - gen_dp1; gen_dp1 = g_gen_drop; uint64_t drx_d = g_dist_rx - dist_rx1; dist_rx1 = g_dist_rx; uint64_t dtx_d = g_dist_tx - dist_tx1; dist_tx1 = g_dist_tx; uint64_t ddp_d = g_dist_drop- dist_dp1; dist_dp1 = g_dist_drop; double gen_tx_mpps = (sec_1s>0? (double)gtx_d/sec_1s:0)/1e6; double gen_dp_mpps = (sec_1s>0? (double)gdp_d/sec_1s:0)/1e6; double dist_rx_mpps = (sec_1s>0? (double)drx_d/sec_1s:0)/1e6; double dist_tx_mpps = (sec_1s>0? (double)dtx_d/sec_1s:0)/1e6; double dist_dp_mpps = (sec_1s>0? (double)ddp_d/sec_1s:0)/1e6; PERF_LOG("[perf] gen tx=%.2f Mpps drop=%.2f Mpps", gen_tx_mpps, gen_dp_mpps); PERF_LOG("[perf] dist rx=%.2f Mpps tx=%.2f Mpps drop=%.2f Mpps", dist_rx_mpps, dist_tx_mpps, dist_dp_mpps);
    double mean = wrx_sum / (double)NB_WORKERS; double var = 0.0; for (unsigned wi = 0; wi < NB_WORKERS; wi++){ double d = rx_vals[wi] - mean; var += d*d; } var /= (double)NB_WORKERS; double sd = sqrt(var); PERF_LOG("[perf] workers rx stddev=%.2f Kpps", sd);
    double fmean = 0.0; for (unsigned wi = 0; wi < NB_WORKERS; wi++) fmean += (double)g_flow_count_shadow[wi]; fmean /= (double)NB_WORKERS; double fvar = 0.0; for (unsigned wi = 0; wi < NB_WORKERS; wi++){ double fd = (double)g_flow_count_shadow[wi] - fmean; fvar += fd*fd; } fvar /= (double)NB_WORKERS; double fsd = sqrt(fvar); PERF_LOG("[perf] workers flows stddev=%.2f", fsd);
    uint64_t fat_hit_d = g_fat_hits - fat_hit1; fat_hit1 = g_fat_hits; uint64_t fat_mis_d = g_fat_misses - fat_mis1; fat_mis1 = g_fat_misses; uint64_t fat_evc_d = g_fat_evictions - fat_evc1; fat_evc1 = g_fat_evictions; double hits_M = (double)fat_hit_d / 1e6; double mis_M = (double)fat_mis_d / 1e6; double evc_M = (double)fat_evc_d / 1e6; PERF_LOG("[perf] FAT hits=%.2fM misses=%.2fM evictions=%.2fM", hits_M, mis_M, evc_M);
    g_epoch += ticks; for (unsigned wi = 0; wi < NB_WORKERS; wi++) { g_flow_count_shadow[wi] = g_flow_count[wi]; g_flow_count[wi] = 0; }
    unsigned moves = greedy_enabled() ? greedy_reshaper_tick(rx_vals, 8u) : 0u;
    printf("[reta] greedy moves=%u", moves); putchar('\n');
    if (csv) { fflush(csv); }
  }
  if (csv) { fclose(csv); }
  return 0;
}
static void sanity_check(void)
{
  unsigned counts[16] = {0}; for (unsigned i = 0; i < RETA_SZ; ++i) counts[g_reta[i]]++; for (unsigned w = 0; w < NB_WORKERS; ++w) { if (counts[w] == 0) { printf("[sanity] RETA worker %u has 0 entries", w); putchar('\n'); } } if (rte_get_tsc_hz() == 0) { puts_nl("[sanity] invalid TSC hz (0)"); } if (!g_fat) { puts_nl("[sanity] FAT not allocated"); }
}
static void create_mempools(void)
{
  unsigned nb_mbufs = 8192u + NB_WORKERS * 4096u; g_mpool = rte_pktmbuf_pool_create("mp", nb_mbufs, POOL_CACHE, 0, MBUF_DATAROOM, rte_socket_id()); if (!g_mpool) rte_exit(EXIT_FAILURE, "mempool (mbuf) create failed: %s", rte_strerror(rte_errno)); g_pipe_pool = rte_mempool_create("pipe_pool", 65536u, sizeof(struct dist_item), POOL_CACHE, 0, NULL, NULL, NULL, NULL, rte_socket_id(), 0); if (!g_pipe_pool) rte_exit(EXIT_FAILURE, "mempool (pipe) create failed: %s", rte_strerror(rte_errno));
}
static void create_rings(void)
{
  char rpfx[16]; snprintf(rpfx, sizeof(rpfx), "%d", getpid()); char name[64]; snprintf(name, sizeof(name), "RQ_INGRESS_%s", rpfx); g_ingress_ring = rte_ring_create(name, RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ); if (!g_ingress_ring) rte_exit(EXIT_FAILURE, "ingress ring create failed: %s", rte_strerror(rte_errno)); snprintf(name, sizeof(name), "RQ_DIST_PIPE_%s", rpfx); g_dist_pipe = rte_ring_create(name, PIPE_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ); if (!g_dist_pipe) rte_exit(EXIT_FAILURE, "dist pipe create failed: %s", rte_strerror(rte_errno)); for (unsigned i = 0; i < NB_WORKERS; i++) { snprintf(name, sizeof(name), "RQ_WR_%u_%s", WORKERS[i], rpfx); g_worker_rings[i] = rte_ring_create(name, RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ); if (!g_worker_rings[i]) rte_exit(EXIT_FAILURE, "worker ring %u create failed: %s", WORKERS[i], rte_strerror(rte_errno)); }
  for (unsigned i = 0; i < NB_WORKERS; i++) { snprintf(name, sizeof(name), "RQ_TX_%u_%s", WORKERS[i], rpfx); g_tx_rings[i] = rte_ring_create(name, RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ); if (!g_tx_rings[i]) rte_exit(EXIT_FAILURE, "tx ring %u create failed: %s", WORKERS[i], rte_strerror(rte_errno)); }
}
static void build_reta(void)
{
  for (unsigned i = 0, w = 0, c = 0; i < RETA_SZ; ++i) { g_reta[i] = w; if (++c == 32u) { c = 0; if (++w == NB_WORKERS) w = 0; } } for (int i = (int)RETA_SZ - 1; i > 0; --i) { int j = (int)(prng() % (uint32_t)(i + 1)); uint8_t t = g_reta[i]; g_reta[i] = g_reta[j]; g_reta[j] = t; }
}
static void create_fat(void)
{
  g_fat = (uint64_t*)rte_zmalloc_socket("fat", FAT_SIZE * sizeof(uint64_t), RTE_CACHE_LINE_SIZE, rte_socket_id()); if (!g_fat) rte_exit(EXIT_FAILURE, "FAT allocate failed: %s", rte_strerror(rte_errno));
}
int main(int argc, char **argv)
{
  (void)argc; (void)argv; signal(SIGINT, on_signal); signal(SIGTERM, on_signal); const char *eal_argv[] = { "software-rss", "-l", "2,3,4,5,6,7,8-15", "--main-lcore", "2", "--file-prefix", "rss1", "--huge-unlink" }; int eal_argc = (int)(sizeof(eal_argv)/sizeof(eal_argv[0])); int ret = rte_eal_init(eal_argc, (char**)eal_argv); if (ret < 0) rte_exit(EXIT_FAILURE, "EAL init failed"); setvbuf(stdout, NULL, _IOLBF, 0); build_flows_and_wheel(); build_header_templates(); build_reta(); banner(); if (!rte_lcore_is_enabled(PERF_CORE) || !rte_lcore_is_enabled(DISTA_CORE) || !rte_lcore_is_enabled(DISTB_CORE) || !rte_lcore_is_enabled(GEN_CORE) || !rte_lcore_is_enabled(SINK_CORE)) rte_exit(EXIT_FAILURE, "Perf/Distributor-A/Distributor-B/generator/sink core not enabled (-l)." ); for (unsigned i = 0; i < NB_WORKERS; i++) { if (!rte_lcore_is_enabled(WORKERS[i])) rte_exit(EXIT_FAILURE, "Worker core %u not enabled (-l).", WORKERS[i]); }
  create_mempools(); create_rings(); create_fat(); sanity_check(); for (unsigned i = 0; i < NB_WORKERS; i++) { rte_eal_remote_launch(worker_main, (void*)(uintptr_t)i, WORKERS[i]); }
  rte_eal_remote_launch(distB_main, NULL, DISTB_CORE); rte_eal_remote_launch(distA_main, NULL, DISTA_CORE); rte_eal_remote_launch(perf_main, NULL, PERF_CORE); rte_eal_remote_launch(gen_main, NULL, GEN_CORE); rte_eal_remote_launch(sink_main, NULL, SINK_CORE);
  unsigned lc; RTE_LCORE_FOREACH_WORKER(lc) { if (lc != PERF_CORE && lc != DISTA_CORE && lc != DISTB_CORE && lc != GEN_CORE && lc != SINK_CORE) { rte_eal_wait_lcore(lc); } }
  rte_eal_wait_lcore(DISTA_CORE); rte_eal_wait_lcore(DISTB_CORE); rte_eal_wait_lcore(PERF_CORE); rte_eal_wait_lcore(GEN_CORE); rte_eal_wait_lcore(SINK_CORE); rte_eal_cleanup(); return 0;
}
/* ---------------------------------------------------------------
 * Revision History (append-only; keep <=80 cols)
 * v1.9.7 (2026-01-05 01:27:14 GMT+08:00)
 * - NEW: GREEDY on/off via env GREEDY (default: on).
 * - Script flag --greedy on|off exports GREEDY to app.
 * - Minimal greedy reshaper gated by GREEDY; logs moves.
 * v1.9.3 (previous)
 * - FIX: Build error resolved; retain rate/env controls.
 * - PERF CSV path: worker_stats_v193.csv.
 * --------------------------------------------------------------- */
