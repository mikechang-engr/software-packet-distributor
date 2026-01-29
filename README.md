# Software Packet Distributor (SPD)

**A software-only, flow-aware Ethernet packet distribution framework for DPDK that dynamically reshapes traffic and workload using a greedy algorithm to improve fairness and load balance across multiple cores in an embedded networking system. Validated on the NXP LX2160A platform.**

---

## Overview

> **Status:** Early open-source baseline (v1.0.0). Actively evolving toward TISF 2027.

The **Software Packet Distributor (SPD)** is a DPDK-based, software-only packet distribution framework designed for embedded multicore networking systems. It addresses the limitations of static RSS (5-tuple hashing), which can suffer from load imbalance when traffic contains skewed or elephant flows.

SPD introduces a **Greedy Reshaper** that continuously observes runtime telemetry and performs bounded, in-place reshaping of flow-to-core mappings. The result is improved fairness and more stable utilization across worker cores, without relying on NIC-specific hardware features.

---

## Motivation

Traditional RSS-based packet steering is fast but static. In real traffic mixes, a small number of high-volume flows can overload individual cores while others remain underutilized. Hardware indirection tables are vendor-specific and coarse-grained, and kernel-level steering mechanisms are not suitable for high-speed DPDK pipelines.

SPD is motivated by three principles:

- **Software-only**: no dependency on NIC vendor features or hardware offload
- **Flow-aware**: observe per-flow and per-bucket behavior at runtime
- **Workload-aware**: reshape assignments based on actual worker load

---

## High-Level Architecture

SPD uses a simple, reproducible software pipeline built entirely with DPDK primitives:

```
Generator → Distributor-A → Greedy Reshaper → Distributor-B → Workers → Sink
```

- **Distributor-A** performs initial flow hashing and fast-path lookup
- **Greedy Reshaper** monitors telemetry and edits bucket ownership in place
- **Distributor-B** forwards packets to worker rings
- **Workers** process packets on dedicated cores

The Greedy Reshaper is co-located with the performance sampling core to minimize overhead and maintain a tight feedback loop.

---

## Greedy Reshaper

The Greedy Reshaper is a lightweight, deterministic heuristic controller:

- Maintains per-bucket telemetry (packet counts, miss rate, EWMA mass)
- Identifies overloaded and underutilized worker cores
- Selectively migrates eligible buckets from hot cores to cool cores
- Enforces strict move budgets and cooldown rules to prevent oscillation

This approach provides real-time adaptation with low overhead, making it suitable for embedded multicore systems.

---

## Platform Validation

SPD has been validated on the following reference platform:

- **Board**: NXP LX2160A-RDB
- **CPU**: 16× Arm Cortex-A72 @ 2.2 GHz
- **Memory**: 32 GB DDR4 ECC
- **OS**: Ubuntu 20.04 (LSDK 21.08)
- **Kernel**: Linux 5.10.35
- **DPDK**: 19.11.7
- **HugeTLB**: 1 GB pages (fallback to 2 MB)

The design is NICless and DPAA2-independent by intent.

---

## Build

The project uses a simple Makefile-based build flow:

```bash
make
```

The Makefile discovers DPDK using `pkg-config` and links against the required DPDK libraries.

---

## Run

Example run commands:

```bash
# Target by Gbps
./start-software-rss.sh --gbps 10 --duration 120

# Target by MPPS
./start-software-rss.sh --mpps 5 --elephants off
```

### Runtime Flags

- `--gbps N`        Target throughput in Gbps
- `--mpps N`        Target rate in MPPS (takes precedence)
- `--elephants on|off`
- `--duration SEC`  Run duration

The start script automatically mounts HugeTLB filesystems and configures huge pages as needed.

---

## Traffic Profile (Reference)

- 10 Gbps @ 64-byte packets
- UDP/TCP approximately 50/50
- ~1024 mice flows
- Optional 3 elephant flows (~10% traffic each)

This profile is intended to stress fairness and load balance across worker cores.

---

## Repository Layout

```
software-packet-distributor/
├─ src/
│  ├─ distributor/
│  └─ greedy_reshaper/
├─ include/
├─ docs/
│  ├─ architecture.md
│  └─ diagrams/
├─ benchmarks/
├─ README.md
├─ LICENSE
└─ .gitignore
```

---

## Project Status and Roadmap

This repository represents the baseline open-source release of SPD.

Planned milestones:

- Community feedback via DPDK, NXP, and platform forums
- Expanded benchmarking and comparison against static RSS
- Refinement of reshaping heuristics and telemetry
- Documentation and presentation for Taiwan International Science Fair (TISF) 2027

---

## License

This project is licensed under the **BSD 3-Clause License**. See the `LICENSE` file for details.

---

## Author

**Mike Chang**  
Email: mikechang.engr@gmail.com

---
