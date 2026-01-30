/*
 * software-packet-distributor
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Mike Chang
 * Author: Mike Chang <mikechang.engr@gmail.com>
 */
#pragma once
#include "defs.h"
static inline uint32_t rotl32(uint32_t x, int r){ return (x<<r) | (x>>(32-r)); }
static inline uint64_t rotl64(uint64_t x, int r){ return (x<<r) | (x>>(64-r)); }
uint32_t xxh32(const void* input, size_t len, uint32_t seed);
uint64_t xxh64(const void* input, size_t len, uint64_t seed);
extern const uint32_t XXH32_SEED; extern const uint64_t XXH64_SEED;
