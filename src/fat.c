/*
 * software-packet-distributor
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Mike Chang
 * Author: Mike Chang <mikechang.engr@gmail.com>
 */
#include "fat.h"
#include "globals.h"
uint64_t fat_get(uint32_t idx){ return g_fat[idx]; }
uint64_t fat_fp56(uint64_t u){ return u>>8; }
uint8_t fat_W3(uint64_t u){ return (uint8_t)((u>>5)&0x07); }
uint8_t fat_A5(uint64_t u){ return (uint8_t)(u & 0x1F); }
uint64_t fat_pack(uint64_t fp56,uint8_t W3,uint8_t A5){ return (fp56<<8) | (((uint64_t)W3 & 0x7)<<5) | ((uint64_t)A5 & 0x1F); }
uint64_t fat_set_age(uint64_t u,uint8_t A5){ return (u & ~0x1FULL) | ((uint64_t)A5 & 0x1F); }
int fat_lookup_tag(uint64_t fp56,uint64_t h64,uint16_t *out_wi){ uint32_t mask=FAT_SIZE-1u; uint32_t idx=(uint32_t)h64 & mask; rte_prefetch0(&g_fat[idx]); for(unsigned p=0;p<8u;++p){ uint64_t u=fat_get(idx); if(u==0) break; if(fat_fp56(u)==fp56){ *out_wi=(uint16_t)fat_W3(u); g_fat[idx]=fat_set_age(u,(uint8_t)(g_epoch & 31)); return 1;} idx=(idx+1u)&mask;} return 0; }
void fat_insert_tag(uint64_t fp56,uint64_t h64,uint16_t wi){ uint32_t mask=FAT_SIZE-1u; uint32_t idx=(uint32_t)h64 & mask; int empty=-1; uint8_t nowA=(uint8_t)(g_epoch & 31); uint32_t oldest_idx=idx; uint8_t oldest_delta=0; for(unsigned p=0;p<8u;++p){ uint64_t u=fat_get(idx); if(u==0){ empty=(int)idx; break; } uint8_t a=fat_A5(u); uint8_t delta=(uint8_t)((nowA-a)&31); if(delta>oldest_delta){ oldest_delta=delta; oldest_idx=idx;} idx=(idx+1u)&mask;} uint32_t tgt=(empty>=0)?(uint32_t)empty:oldest_idx; if(empty<0) g_fat_evictions++; g_fat[tgt]=fat_pack(fp56,(uint8_t)wi,nowA);}