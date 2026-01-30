# software-packet-distributor
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Mike Chang
# Author: Mike Chang <mikechang.engr@gmail.com>
CC ?= cc
CFLAGS += -O2 -g -Wall -Wextra -Wno-unused-parameter -std=gnu11 -D_GNU_SOURCE -include rte_config.h -march=armv8-a+crc -moutline-atomics
INCLUDES += -I/usr/local/include -Iinclude
LDFLAGS += -L/usr/local/lib -Wl,--as-needed -lrte_node -lrte_graph -lrte_bpf -lrte_flow_classify -lrte_pipeline -lrte_table -lrte_port -lrte_fib -lrte_ipsec -lrte_vhost -lrte_stack -lrte_security -lrte_sched -lrte_reorder -lrte_rib -lrte_regexdev -lrte_rawdev -lrte_pdump -lrte_power -lrte_member -lrte_lpm -lrte_latencystats -lrte_kni -lrte_jobstats -lrte_ip_frag -lrte_gso -lrte_gro -lrte_eventdev -lrte_efd -lrte_distributor -lrte_cryptodev -lrte_compressdev -lrte_cfgfile -lrte_bitratestats -lrte_bbdev -lrte_acl -lrte_timer -lrte_hash -lrte_metrics -lrte_cmdline -lrte_pci -lrte_ethdev -lrte_meter -lrte_net -lrte_mbuf -lrte_mempool -lrte_rcu -lrte_ring -lrte_eal -lrte_telemetry -lrte_kvargs -lm
BIN = software-packet-distributor
SRC = \
  src/main.c \
  src/globals.c \
  src/hash.c \
  src/flow.c \
  src/fat.c \
  src/core_distributor.c \
  src/core_generator.c \
  src/core_worker.c \
  src/perf.c
all: $(BIN)
$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)
clean:
	rm -f $(BIN)
