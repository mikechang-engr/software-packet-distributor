# Software Packet Distributor (SPD) — v1.0.5

*A DPDK-based, software-only, flow-aware packet distributor with a Greedy Reshaper for dynamic, low-overhead load balancing.*

[![License](https://img.shields.io/badge/License-BSD--3--Clause-blue.svg)](LICENSE)
![Version](https://img.shields.io/badge/version-v1.0.5-green.svg)
![Status](https://img.shields.io/badge/status-active%20development-orange.svg)
![DPDK](https://img.shields.io/badge/DPDK-19.11.7%20compatible-informational.svg)
![Platform](https://img.shields.io/badge/Platform-LX2160A--RDB-lightgrey.svg)

**Release date:** 2026-01-30 21:17 (Taipei, GMT+08:00)

**Author:** Mike Chang (mikechang.engr@gmail.com)  
**Repository:** https://github.com/mikechang-engr/software-packet-distributor

---

## Overview
The **Software Packet Distributor (SPD)** is a DPDK-based packet distribution framework for embedded multicore networking systems. It addresses the limitations of static RSS by introducing a **Greedy Reshaper** that adaptively reassigns flow buckets to worker cores based on runtime telemetry—improving fairness, utilization, and stability without relying on NIC-specific features.

---

## Status & Latest Developments (v1.0.5)
- **Build portability:** `lcg32_local()` moved to *file scope* (non-standard nested function removed); **runtime behavior unchanged**.
- **Perf CSV:** output filename now **`/var/log/software-packet-distributor/worker_stats_v105.csv`**.
- **Start script:** refined **INT → TERM → KILL** escalation and **newline-safe logging**.

---

## Motivation
Traditional RSS-based packet steering is fast but fundamentally **static**. While RSS classifies traffic by 5-tuple flows, it ignores the **weight imbalance** between flows. In real network conditions, a small number of **high-volume (elephant) flows** can overwhelm specific CPU cores, leaving others underutilized—hurting throughput, fairness, and stability.

Hardware solutions offer limited help: NIC indirection tables are **vendor-specific** and **coarse-grained**. Kernel-level steering mechanisms are not suitable for **high-speed, user-space DPDK pipelines**, where latency and overhead must remain tightly bounded.

The **Software Packet Distributor (SPD)** is motivated by the need for a **fully software-defined**, adaptable distribution path that remains portable and predictable across platforms. Its design is guided by three principles:

- **Software-only** — No dependence on NIC-specific capabilities or offloads; deterministic behavior across DPDK environments.
- **Flow-aware** — Continuously observes per-flow and per-bucket characteristics to identify hotspots and imbalances.
- **Workload-aware** — Dynamically reshapes bucket-to-core assignments based on real-time worker utilization, stabilizing system behavior under skewed traffic.

---

## High-Level Architecture
SPD uses a clean, reproducible, DPDK-only pipeline:

```
Generator
    ↓
Distributor-A (initial hashing + bucket lookup)
    ↓
Greedy Reshaper (runtime telemetry + in-place bucket ownership edits)
    ↓
Distributor-B (updated flow-to-core mapping)
    ↓
Workers (per-core packet processing)
    ↓
Sink
```

- **Distributor-A**: performs initial hashing and fast-path bucket lookup using a compact **FAT tag cache** (8-byte entry, 56+3+5 packing); on miss, falls back to **RETA**.
- **Greedy Reshaper**: collects telemetry and applies **bounded, in-place** bucket reassignments.
- **Distributor-B**: forwards packets using the updated mapping.
- **Workers**: dedicated cores for packet processing.

---

## Key Innovations
- **Software-only portability** — No vendor NIC features or hardware offloads; consistent behavior across platforms.
- **Flow-aware + workload-aware balancing** — Uses per-flow/per-bucket observations and actual worker utilization to guide reassignment.
- **Greedy, bounded updates** — Minimal, targeted changes prevent global table rebuilds and avoid disruptive bursts (in v1.0.5, updates migrate selected **RETA** entries in place).

---

## Greedy Reshaper: Algorithm Overview
**Goal:** minimize load imbalance while preserving stability.

**Control loop (each sampling interval):**
1. Identify **overloaded** and **underloaded** workers from RX Kpps.
2. Rank contributing **hot buckets/flows** on overloaded workers.
3. **Greedily** move the smallest set of buckets with the largest balance gain.
4. Apply **in-place** updates to the mapping; avoid full rehash/rebuild.

### Mechanism (Inputs / Outputs & Side-Effects)
**Inputs**
- Per-worker RX rates `rx_vals[NB_WORKERS]` (in Kpps), computed by the perf core each tick (8 workers defined: lcores 8..15).
- Move budget `max_moves` = **8** edits per interval (bounded control cost).
- Software **RETA**: `g_reta[RETA_SZ]` with **RETA_SZ=256** and `RETA_MASK=RETA_SZ-1`.
- Gate `GREEDY=on|off` via environment variable — **default ON** when unset.

**Computation**
1. Identify **hot** (max Kpps) and **cold** (min Kpps) workers from `rx_vals`.
2. Scan the RETA and **flip entries that point to `hot` → `cold`**, up to `max_moves`.
   - v1.9.7: scan start is **pseudo-random**.
   - v1.0.5: scan start is **deterministic**.

**Outputs & Side-effects**
- Returns the number of edits performed (`moves`), logged as: `"[reta] greedy moves=<n>"`.
- **In-place** RETA updates only; no global remap or rehash occurs.

**Timing & Telemetry**
- Perf core ticks roughly **once per second** (TSC-based), writes CSV rows to
  **`/var/log/software-packet-distributor/worker_stats_v105.csv`**, then invokes the reshaper with a small move budget.

**Complexity**
- Worst-case scan is `O(RETA_SZ)`; **edit cost bounded** by `max_moves` ⇒ predictable overhead.

---

## Platform & Validation (Tested)
- **Board:** NXP **LX2160A-RDB (Rev 2.0)**, **FPGA v8.0**
- **CPU:** **16 × ARM Cortex-A72 @ 2.2 GHz**
- **Memory:** **32 GB DDR4 ECC**, CL=22, CS0+CS1, 256B interleaving
- **Clocks:** Bus **750 MHz**, DDR **3200 MT/s**
- **Storage:** SPI NOR Micron **mt35xu512aba 64 MiB ×2**; **eMMC 116 GiB** (p1–p4)
- **Firmware:** **BL2 v2.4 (LSDK-21.08)**, **BL31 v2.4 (LSDK-21.08)**, **U-Boot 2021.04**
- **Kernel/Distro:** **Linux 5.10.35 (SMP PREEMPT)**, **NXP LSDK 21.08 (Ubuntu 20.04)**, **systemd 245.4-4ubuntu3.11**
- **Toolchain:** **GCC 9.3.0**, **binutils 2.34**
- **DPDK:** **19.11.7-0ubuntu0.20.04.1** (NICless: vdev **PCAP/NULL** only)
- **Hugepages (boot args):** `default_hugepagesz=1024m hugepagesz=1024m hugepages=2`
- **Filesystems:** **Root EXT4 on mmcblk1p4**; **Boot EXT4 on mmcblk1p2**

> For general usage, SPD also supports mounting **1 GiB** hugepages at `/mnt/huge-1G` (preferred) with 2 MiB at `/mnt/huge` as fallback.

---

## Quick Start

### Prerequisites
- NXP LX2160A-RDB Rev 2.0 (16 × A72 @ 2.2 GHz), LSDK 21.08 (Ubuntu 20.04), Linux 5.10.35
- DPDK **19.11.7-0ubuntu0.20.04.1** (NICless vdev PCAP/NULL supported)
- GCC 9.3.0, binutils 2.34
- Huge pages mounted at `/mnt/huge-1G` (1 GiB) or `/mnt/huge` (2 MiB fallback)

### Build
```bash
make -j"$(nproc)"
```

### Run (start script with sane defaults)
```bash
./scripts/start-software-packet-distributor.sh \
  --mpps 5.0 \
  --duration 20 \
  --elephants on \
  --greedy on
# Note: --mpps takes precedence over --gbps
```
- Start script features **INT → TERM → KILL** signal escalation and **newline-safe logging**.

### Core Layout (example mapping)
- Core-0,1: Linux housekeeping/IRQs (reserved)
- Core-2: DPDK main
- Core-3: sink
- Core-4: generator
- Core-5: perf + Greedy Reshaper
- Core-6: distributor-A
- Core-7: distributor-B
- Cores 8–15: workers (eight workers; four 2-core clusters share 1MB L2)

### Environment Knobs
- `TARGET_MPPS` or `TARGET_GBPS` — traffic rate (**MPPS overrides GBPS**)
- `ELEPHANTS=on|off` — enable **3 elephant flows (~10% each)**
- `GREEDY=on|off` — toggle Greedy Reshaper

### Metrics & Logs
- Per-worker **KPPS, drops, flow counts, FAT stats** logged each second to  
  `/var/log/software-packet-distributor/worker_stats_v105.csv`  
  (CSV header: `epoch,worker,rx_kpps,tx_kpps,drops,flows,fat_hits,fat_misses,fat_evictions`).

---

## Performance Highlights (to be filled with plots)
Include plots/tables for:
- Worker utilization variance (↓) under skewed traffic
- P99 latency stability (↑) across elephant-flow scenarios
- Throughput vs. worker count scaling
- Reassignment cost per interval (CPU overhead)

> Provide one sample `worker_stats_v105.csv` (≥ 60 s) and we will generate publication-ready figures under `docs/`.

---

## Roadmap (toward TISF 2027)
- Adaptive multi-stage reshaping heuristics
- Congestion-aware (queue-aware) bucket ranking
- Optional ML-guided flow hotness prediction
- Reproducible benchmarks with public datasets and traffic profiles

---

## Contributing
Issues and PRs are welcome. Please open a discussion for architecture changes before submitting large patches.

**Coding style note:** prefer **newline-safe output** (avoid embedding "\n" in string literals); use `puts(...)`, `putchar(10)`, or `fputc(10, f)` for line termination.

---

## License
**BSD-3-Clause** (see `LICENSE`).

---

## Citation & Links
- Repository: https://github.com/mikechang-engr/software-packet-distributor
- `CITATION.cff` included for formal citation (APA/BibTeX)

---

## Acknowledgments
- DPDK community and maintainers
- Embedded networking researchers and practitioners
- Hardware validation on **NXP LX2160A** platform

---

> © 2026 Mike Chang. All rights reserved. Released under the BSD-3-Clause license.
