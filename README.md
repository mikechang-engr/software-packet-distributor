# software-packet-distributor

**Version:** v1.0.5 (Taipei, GMT+08:00)

### What’s fixed in v1.0.5
- Build fix: moved `lcg32_local()` to file scope in `globals.c` (no nested function definitions).
- CSV output filename bumped to `worker_stats_v105.csv`.
- Start script: typo fix for `2>/dev/null` (SIGTERM path).

### Quick Start
- Build: `make`
- Run: `scripts/start-software-packet-distributor.sh --mpps 5.0 --duration 20`

### Layout
- `src/`, `include/`, `scripts/`, `Makefile`, `README.md`, `LICENSE`

### License
BSD 3‑Clause.

**Author:** Mike Chang <mikechang.engr@gmail.com>
