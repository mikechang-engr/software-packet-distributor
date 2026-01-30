/*
 * software-packet-distributor
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Mike Chang
 * Author: Mike Chang <mikechang.engr@gmail.com>
 */
#pragma once
#include "defs.h"
#define FAT_SIZE 2048u
uint64_t fat_get(uint32_t idx); uint64_t fat_fp56(uint64_t u);
uint8_t fat_W3(uint64_t u); uint8_t fat_A5(uint64_t u);
uint64_t fat_pack(uint64_t fp56, uint8_t W3, uint8_t A5);
uint64_t fat_set_age(uint64_t u, uint8_t A5);
int fat_lookup_tag(uint64_t fp56, uint64_t h64, uint16_t *out_wi);
void fat_insert_tag(uint64_t fp56, uint64_t h64, uint16_t wi);
