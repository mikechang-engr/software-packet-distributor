/*
 * software-packet-distributor
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Mike Chang
 * Author: Mike Chang <mikechang.engr@gmail.com>
 */
#pragma once
#include "defs.h"
unsigned greedy_reshaper_tick(const double *rx_vals, unsigned max_moves);
bool greedy_enabled(void); int perf_main(void *arg);
