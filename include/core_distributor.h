/*
 * software-packet-distributor
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Mike Chang
 * Author: Mike Chang <mikechang.engr@gmail.com>
 */
#pragma once
#include "defs.h"
uint16_t pick_worker(uint32_t h);
int distA_main(void *arg); int distB_main(void *arg);
void track_flow(unsigned wi, uint32_t sig);
