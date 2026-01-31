# software-packet-distributor v1.0.5 — 2026-01-30 21:17 (Taipei, GMT+08:00)

[![License](https://img.shields.io/badge/license-BSD--3--Clause-blue.svg)](LICENSE)

The Software Packet Distributor (SPD) is a software‑only, flow‑aware Ethernet packet distribution framework built on DPDK, designed for embedded multicore systems. Its goal is to overcome static RSS’s weaknesses—especially load imbalance caused by elephant flows—by dynamically reshaping flow‑to‑core assignments using a Greedy Reshaper.

High-performance **DPDK**-based packet distributor featuring a compact **FAT** tag
cache (8-byte entry; 56+3+5 packing) with **RETA** fallback, **XXH32/XXH64** flow
signatures, and an optional **Greedy Reshaper** that incrementally remaps RETA
entries to balance per-worker load under mixed mice/elephant traffic.

> **Author:** **Mike Chang** (<mikechang.engr@gmail.com>)  
> **Repository:** https://github.com/mikechang-engr/software-packet-distributor  
> **License:** BSD 3‑Clause

---

## Latest Developments (v1.0.5)
- **Build portability:** `lcg32_local()` moved to *file scope* in `globals.c` to
  remove non-standard nested function definition. Runtime behavior unchanged.
- **Perf CSV:** output filename updated to `worker_stats_v105.csv`.
- **Start script:** refined INT→TERM→KILL escalation and newline‑safe logging.

> This README expands on the original v1.0.0 README you uploaded and keeps the
> same overall organization while adding more technical detail.

---

## System Architectural Summary
A five-component pipeline: **generator → Distributor‑A → Distributor‑B → workers → sink**.

- **Generator** synthesizes 64B UDP/TCP frames driven by `TARGET_MPPS` or
  `TARGET_GBPS` (MPPS takes precedence) and places bursts on the ingress ring.
- **Distributor‑A** extracts a 13‑byte 5‑tuple signature, computes **XXH64/XXH32**,
  and probes the **FAT**: hit ⇒ worker; miss ⇒ **RETA** fallback.
- **Distributor‑B** consolidates, buckets, and bursts traffic to per‑worker rings;
  tracks per‑worker flow signatures for telemetry.
- **Workers (N=8)** push bursts to per‑worker TX rings; **Sink** reclaims mbufs.
- **Perf** (telemetry) logs kpps/drops/flows/FAT stats each second and may trigger
  **Greedy Reshaper** to migrate a small number of RETA entries from the hottest to
  the coldest worker.

### ASCII Architecture (≤ 80 cols)
```
             +----------------+
             |   Generator    |
             |   (L4 frames)  |
             +--------+-------+
                      |
                      v
            +-------------------+
            |  Distributor-A    |  XXH64/32 -> FAT hit? yes -> worker
            | (sig + FAT/RETA)  |                  no -> RETA[w]
            +---------+---------+
                      |
                      v
            +-------------------+
            |  Distributor-B    |  bucket->burst to worker rings
            +---------+---------+
                      |
     +----------------+------------------ ... --------------+
     v                v                             v
+---------+     +-----------+                 +-----------+
| Worker0 | ... | Worker7   | ...             |   Sink    |
+----+----+     +-----+-----+                 +-----+-----+
     |                |                             |
     v                v                             v
  TX ring          TX ring                        mbuf free
```

---

## Platform / Environment
- **Target**: NXP **LX2160A‑RDB** (DPDK 19.11‑compatible environment)
- **Hugepages**: prefer **1 GiB × 4** at `/mnt/huge-1G`; fallback **2 MiB** at
  `/mnt/huge`
- **Logs/CSV**: `/var/log/software-packet-distributor/worker_stats_v105.csv`

---

## Core Layout (example)
- `--main-lcore`: **2**
- **Perf**: 5; **Dist‑A**: 6; **Dist‑B**: 7; **Generator**: 4; **Sink**: 3
- **Workers**: **8–15** (8 workers)

---

## Build
### Dependencies
- DPDK development headers & libs (19.11‑series compatible)
- `make`, toolchain for aarch64 (LX2160A), `pkg-config` (optional)

### Compile
```bash
make -j"$(nproc)"
```

---

## Run
Use the provided start script (newline‑safe logging; auto‑mounts hugetlbfs when
needed):

```bash
./scripts/start-software-packet-distributor.sh   --mpps 5.0   --duration 20   --elephants on   --greedy on
# Tip: --mpps takes precedence over --gbps
```

### Environment knobs
- `TARGET_MPPS` or `TARGET_GBPS` – traffic rate (MPPS overrides GBPS)
- `ELEPHANTS=on|off`           – enable 3 elephant flows (~10% each)
- `GREEDY=on|off`              – enable Greedy Reshaper

### Metrics & logs
- Per‑worker KPPS, drops, flow counts, FAT stats each second →
  `/var/log/software-packet-distributor/worker_stats_v105.csv`

---

## Traffic Profile (default)
- 64‑byte frames; mixed UDP/TCP (wheel‑driven) with **mice + 3 elephants**
  (~10% each when enabled) and shuffle per‑second for dynamic flow churn.

---

## Package Tree (key items)
```
.
├─ include/           # public headers (defs, globals, hash, flow, fat, cores, perf)
├─ src/               # C sources (globals, hash, flow, fat, core_* , perf, main)
├─ scripts/           # start-software-packet-distributor.sh (launch & hugepages)
├─ Makefile           # build (DPDK libs/flags)
├─ LICENSE            # BSD 3-Clause
└─ README.md          # this file
```

---

## Coding Conventions
- **Newline‑safe output**: avoid embedding "
" in string literals. Use `puts(...)`,
  `putchar(10)`, or `fputc(10, f)` for line termination.

---

## Citation
This repository includes **`CITATION.cff`** so it can be cited formally (APA or
BibTeX) from the repo sidebar.

---

## Revision History
- **v1.0.5** — build portability fix (`lcg32_local` at file scope), updated CSV
  to `worker_stats_v105.csv`, improved start script signaling and newline safety.
- **v1.0.0** — first public split/layout; GREEDY gate; newline‑safe printing;
  CSV benchmarks (v100).
