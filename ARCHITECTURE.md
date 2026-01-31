# Software Packet Distributor (SPD) — Architecture Diagram (v1.0.5)

**Author:** Mike Chang  
**Contact:** mikechang.engr@gmail.com  
**Date:** 2026-01-31 19:08 (Taipei, GMT+08:00)  
**Copyright:** © 2026 Mike Chang. All rights reserved.

> This document provides a top‑down ASCII architecture diagram for the SPD
> data path and briefly describes the packet flow, LX2160A core layout, and
> implementation data structures.

---

## Top‑Down ASCII Architecture (≤80 columns)

```
+----------------------+ 
|      Generator       |  (Core 4)
+----------+-----------+
           |
           v
+----------------------+
|     Ingress Ring     |
|   (RQ_INGRESS_*)     |
+----------+-----------+
           |
           v
+----------------------+
|    Distributor-A     |  (Core 6)
|  - FAT tag (8B)      |
|  - XXH32/XXH64       |
|  - Miss -> RETA      |
+----------+-----------+
           |
           v
+----------------------+
|    Distributor-B     |  (Core 7)
|  - Batch by worker   |
+----+----+----+----+--+
     |    |    |    |
     v    v    v    v
+----+-----------------+
|  Workers[0..7]       |
|  (lcores 8..15)      |
+----+-----------------+
     |
     v
+----------------------+
|         Sink         |  (Core 3)
+----------------------+

        ^
        | per-second KPPS, flow counts, FAT stats
+----------------------+      RETA edits (bounded, in-place)
|     Perf Monitor     |------------------------------->
|  (Core 5)            |
|  CSV + Greedy tick   |
+----------------------+
```

### How to Read the Diagram (Legend)
- Rectangles denote **DPDK roles/stages**; text in parentheses indicates the **CPU core**.
- Vertical arrows `|/v` show **data-path flow** (top-to-down).
- The rightward arrow from **Perf Monitor** represents a **control path** that
  may update the RETA (bounded, in-place) each second.
- `Workers[0..7]` maps to **lcores 8–15** (8 workers) in this reference layout.
- Ring names (e.g., `RQ_INGRESS_*`) match the runtime-created ring prefixes.

---

## Packet Flow Description

1. **Generator** (Core 4) synthesizes packets according to the traffic wheel
   (mice + optional elephant flows) and enqueues bursts into the **Ingress
   Ring**. The generator rate is driven by TARGET_MPPS/GBPS (MPPS takes
   precedence).
2. **Distributor‑A** (Core 6) computes a compact flow signature using
   XXH32/XXH64 and performs a fast **FAT tag** lookup. On a hit, it selects the
   cached worker; on a miss, it falls back to the software **RETA** (256
   entries) and inserts a new tag. Items are forwarded to the pipeline ring.
3. **Distributor‑B** (Core 7) groups packets by **worker index** and enqueues
   them to per‑worker rings. Each arrival updates per‑worker flow accounting.
4. **Workers[0..7]** (lcores 8–15) pop from their rings and immediately push to
   TX rings (this reference build is a forwarder stage to the Sink).
5. **Sink** (Core 3) drains TX rings and frees mbufs.
6. **Perf Monitor** (Core 5) ticks ~once per second: logs per‑worker KPPS, flow
   counts, FAT stats to CSV and triggers the **Greedy Reshaper** that performs a
   *bounded* number of in‑place **RETA** edits moving entries from the hottest
   to the coldest worker.

---

## LX2160A CPU Core Assignments

- **Main lcore:** 2 (housekeeping / EAL main)
- **Sink:** 3
- **Generator:** 4
- **Perf Monitor:** 5
- **Distributor‑A:** 6
- **Distributor‑B:** 7
- **Workers:** 8–15 → Workers[0..7]

> Rings: ingress, pipeline (Dist‑A→Dist‑B), per‑worker RX/TX rings.  
> Tables: FAT (2048 entries, 8‑probe window, 5‑bit modular age), RETA (256).

---

## Data Structures (Implementation Notes)
- **Rings:**
  - `RQ_INGRESS_*` (ingress, size 8192), `RQ_DIST_PIPE_*` (pipeline, size 65536),
    `RQ_WR_<lcore>_*` (per-worker RX), `RQ_TX_<lcore>_*` (per-worker TX).  
    *Source: `RING_SIZE=8192`, `PIPE_SIZE=65536`, created in `create_rings()`.*
- **Mempools:**
  - `mp` (mbufs) sized to workload; `pipe_pool` holds distributor items
    (`struct dist_item`). *Source: `create_mempools()`; `MBUF_DATAROOM=2176`,
    `POOL_CACHE=256`.*
- **RETA (software indirection table):** 256 entries, shuffled at init; used on
  FAT miss. *Source: `RETA_SZ=256`, `build_reta()`.*
- **FAT tag cache:** 2048 entries, **8 bytes each** with **56+3+5** packing
  (flow fp56, worker index 3 bits, 5-bit modular age), up to **8 probes** per
  lookup; on insert, replaces the stalest entry in the small window. *Source:
  `FAT_SIZE=2048`, `fat_pack/fat_lookup_tag/fat_insert_tag()`.*
- **Telemetry/CSV:** per-second logging of KPPS, drops, flow counts and FAT
  stats.  
  - In **SPD v1.0.5** (README): `/var/log/software-packet-distributor/worker_stats_v105.csv`.
  - In legacy **software-rss v1.9.7** (source here): `/var/log/software-rss/worker_stats_v197.csv`.
- **Greedy Reshaper:** runtime gated by `GREEDY=on|off` (default ON); performs a
  small, **bounded** number of in‑place **RETA** edits per interval from the
  hottest to the coldest worker.

---

## Notes
- Newline‑safe logging is used throughout; lines are terminated via `puts` or
  `putchar(10)` to remain viewer‑friendly in logs.
- The Greedy Reshaper is runtime‑toggleable via `GREEDY=on|off` (default: ON),
  with a small edit budget per interval to keep control cost predictable.

```
End of document.
```
