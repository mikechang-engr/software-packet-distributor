/*
 * software-packet-distributor
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Mike Chang
 * Author: Mike Chang <mikechang.engr@gmail.com>
 */
#pragma once
#include "defs.h"
enum proto_e { PROTO_UDP = 17, PROTO_TCP = 6 };
typedef struct Flow { uint8_t src_ip[4], dst_ip[4]; enum proto_e proto; uint16_t sport_base, dport_base; } Flow;
extern Flow g_flows[NFLOWS];
void build_flows_and_wheel(void); void build_header_templates(void);
void mutate_flows_chunk(unsigned sec_idx, unsigned cycle_idx); void reshuffle_wheel(void);
uint32_t flow_wheel_next(void);
const uint8_t* flow_template_udp(void); const uint8_t* flow_template_tcp(void);
