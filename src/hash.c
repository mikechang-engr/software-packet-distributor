/*
 * software-packet-distributor
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Mike Chang
 * Author: Mike Chang <mikechang.engr@gmail.com>
 */
#include "hash.h"
#define XXH_PRIME32_1 0x9E3779B1u
#define XXH_PRIME32_2 0x85EBCA77u
#define XXH_PRIME32_3 0xC2B2AE3Du
#define XXH_PRIME32_4 0x27D4EB2Fu
#define XXH_PRIME32_5 0x165667B1u
uint32_t xxh32(const void* input, size_t len, uint32_t seed){ const uint8_t* p=(const uint8_t*)input; const uint8_t* bEnd=p+len; uint32_t h32; if(len>=16){ uint32_t v1=seed+XXH_PRIME32_1+XXH_PRIME32_2; uint32_t v2=seed+XXH_PRIME32_2; uint32_t v3=seed+0; uint32_t v4=seed-XXH_PRIME32_1; const uint8_t* limit=bEnd-16; do{ uint32_t w1,w2,w3,w4; memcpy(&w1,p,4); p+=4; v1+=w1*XXH_PRIME32_2; v1=rotl32(v1,13); v1*=XXH_PRIME32_1; memcpy(&w2,p,4); p+=4; v2+=w2*XXH_PRIME32_2; v2=rotl32(v2,13); v2*=XXH_PRIME32_1; memcpy(&w3,p,4); p+=4; v3+=w3*XXH_PRIME32_2; v3=rotl32(v3,13); v3*=XXH_PRIME32_1; memcpy(&w4,p,4); p+=4; v4+=w4*XXH_PRIME32_2; v4=rotl32(v4,13); v4*=XXH_PRIME32_1; } while(p<=limit); h32=rotl32(v1,1)+rotl32(v2,7)+rotl32(v3,12)+rotl32(v4,18);} else { h32=seed+XXH_PRIME32_5;} h32 += (uint32_t)len; while((p+4)<=bEnd){ uint32_t k1; memcpy(&k1,p,4); p+=4; h32 += k1*XXH_PRIME32_3; h32 = rotl32(h32,17)*XXH_PRIME32_4; } while(p<bEnd){ h32 += (*p)*XXH_PRIME32_5; p++; h32 = rotl32(h32,11)*XXH_PRIME32_1; } h32^=h32>>15; h32*=XXH_PRIME32_2; h32^=h32>>13; h32*=XXH_PRIME32_3; h32^=h32>>16; return h32; }
#define XXH_PRIME64_1 0x9E3779B185EBCA87ULL
#define XXH_PRIME64_2 0xC2B2AE3D27D4EB4FULL
#define XXH_PRIME64_3 0x165667B19E3779F9ULL
#define XXH_PRIME64_4 0x85EBCA77C2B2AE63ULL
#define XXH_PRIME64_5 0x27D4EB2F165667C5ULL
uint64_t xxh64(const void* input, size_t len, uint64_t seed){ const uint8_t* p=(const uint8_t*)input; const uint8_t* bEnd=p+len; uint64_t h64; if(len>=32){ uint64_t v1=seed+XXH_PRIME64_1+XXH_PRIME64_2; uint64_t v2=seed+XXH_PRIME64_2; uint64_t v3=seed+0; uint64_t v4=seed-XXH_PRIME64_1; const uint8_t* limit=bEnd-32; do{ uint64_t w1,w2,w3,w4; memcpy(&w1,p,8); p+=8; v1+=w1*XXH_PRIME64_2; v1=rotl64(v1,31); v1*=XXH_PRIME64_1; memcpy(&w2,p,8); p+=8; v2+=w2*XXH_PRIME64_2; v2=rotl64(v2,31); v2*=XXH_PRIME64_1; memcpy(&w3,p,8); p+=8; v3+=w3*XXH_PRIME64_2; v3=rotl64(v3,31); v3*=XXH_PRIME64_1; memcpy(&w4,p,8); p+=8; v4+=w4*XXH_PRIME64_2; v4=rotl64(v4,31); v4*=XXH_PRIME64_1; } while(p<=limit); h64=rotl64(v1,1)+rotl64(v2,7)+rotl64(v3,12)+rotl64(v4,18);} else { h64=seed+XXH_PRIME64_5;} h64 += (uint64_t)len; while((p+8)<=bEnd){ uint64_t k1; memcpy(&k1,p,8); p+=8; k1*=XXH_PRIME64_2; k1=rotl64(k1,31); k1*=XXH_PRIME64_1; h64^=k1; h64=rotl64(h64,27)*XXH_PRIME64_1 + XXH_PRIME64_4; } while(p<bEnd){ h64^=(*p)*XXH_PRIME64_5; p++; h64=rotl64(h64,11)*XXH_PRIME64_1; } h64^=h64>>33; h64*=XXH_PRIME64_2; h64^=h64>>29; h64*=XXH_PRIME64_3; h64^=h64>>32; return h64; }
const uint32_t XXH32_SEED=0x9E3779B1u; const uint64_t XXH64_SEED=0x9E3779B97F4A7C15ULL;
