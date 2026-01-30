# software-packet-distributor
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Mike Chang
# Author: Mike Chang <mikechang.engr@gmail.com>
#!/bin/sh
set -eu
log() { printf "%s" "$*"; printf "
"; }
MNT_1G="/mnt/huge-1G"; MNT_2M="/mnt/huge"
HUGE_1G_COUNT="${HUGE_1G_COUNT:-4}"; HUGE_2M_COUNT="${HUGE_2M_COUNT:-2048}"; RUN_SECS="${RUN_SECS:-32}"
GBPS=""; MPPS=""; ELEPH=""; GREEDY=""
usage(){ printf "%s" "usage: $0 [--gbps N] [--mpps N] [--duration S] [--elephants on|off] [--greedy on|off]"; printf "
"; }
while [ $# -gt 0 ]; do case "$1" in
  --gbps) [ $# -ge 2 ] || { log "[start] missing value for --gbps"; usage; exit 2; }; GBPS="$2"; shift 2;;
  --mpps) [ $# -ge 2 ] || { log "[start] missing value for --mpps"; usage; exit 2; }; MPPS="$2"; shift 2;;
  --duration) [ $# -ge 2 ] || { log "[start] missing value for --duration"; usage; exit 2; }; RUN_SECS="$2"; shift 2;;
  --elephants) [ $# -ge 2 ] || { log "[start] missing value for --elephants"; usage; exit 2; }; case "$2" in on|off) ELEPH="$2";; *) log "[start] --elephants must be on|off"; exit 2;; esac; shift 2;;
  --greedy) [ $# -ge 2 ] || { log "[start] missing value for --greedy"; usage; exit 2; }; case "$2" in on|off) GREEDY="$2";; *) log "[start] --greedy must be on|off"; exit 2;; esac; shift 2;;
  --help|-h) usage; exit 0;; *) log "[start] unknown flag: $1"; usage; exit 2;; esac; done
is_num(){ awk 'BEGIN{ok=ARGV[1] ~ /^[0-9]+(\.[0-9]+)?$/; exit ok?0:1 }' "$1"; }
if [ -n "$MPPS" ]; then is_num "$MPPS" || { log "[start] --mpps must be numeric"; exit 2; }; export TARGET_MPPS="$MPPS"; log "[start] TARGET_MPPS=$TARGET_MPPS"; elif [ -n "$GBPS" ]; then is_num "$GBPS" || { log "[start] --gbps must be numeric"; exit 2; }; export TARGET_GBPS="$GBPS"; log "[start] TARGET_GBPS=$TARGET_GBPS"; fi
[ -n "$ELEPH" ] && export ELEPHANTS="$ELEPH" || export ELEPHANTS="on"; log "[start] ELEPHANTS=$ELEPHANTS"
[ -n "$GREEDY" ] && export GREEDY="$GREEDY" || export GREEDY="on"; log "[start] GREEDY=$GREEDY"
pagesize_of(){ awk -v m="$1" '$2==m && $3=="hugetlbfs"{for(i=4;i<=NF;i++){if($i ~ /pagesize=/){sub(/.*pagesize=/, "", $i); gsub(/,/, "", $i); print $i; exit}}}' /proc/mounts || true; }
ensure_mounts(){ sudo mkdir -p "$MNT_1G" "$MNT_2M"; ps1=$(pagesize_of "$MNT_1G"); [ "$ps1" = "1024M" ] || [ "$ps1" = "1G" ] || { sudo umount "$MNT_1G" 2>/dev/null || true; sudo mount -t hugetlbfs -o pagesize=1G none "$MNT_1G" || true; }; ps2=$(pagesize_of "$MNT_2M"); [ "$ps2" = "2M" ] || [ "$ps2" = "2048k" ] || { sudo umount "$MNT_2M" 2>/dev/null || true; sudo mount -t hugetlbfs -o pagesize=2M none "$MNT_2M" || true; }; }
ensure_counts(){ total_1g=$(awk '/HugePages_Total:/ {print $2}' /proc/meminfo 2>/dev/null || echo 0); free_1g=$(awk '/HugePages_Free:/ {print $2}' /proc/meminfo 2>/dev/null || echo 0); if [ "$free_1g" = "$total_1g" ]; then cur=$(cat /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages 2>/dev/null || echo 0); [ "$cur" = "$HUGE_1G_COUNT" ] || { echo 0 | sudo tee /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages >/dev/null || true; echo "$HUGE_1G_COUNT" | sudo tee /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages >/dev/null || true; }; else log "[start] 1G HugePages in use ($free_1g/$total_1g); skipping 1G reset"; fi; have_2m=$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages 2>/dev/null || echo 0); [ "$have_2m" = "$HUGE_2M_COUNT" ] || echo "$HUGE_2M_COUNT" | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages >/dev/null || true; }
cleanup_stale(){ sudo sh -c "rm -f $MNT_1G/spd1* $MNT_2M/spd1* 2>/dev/null || true"; }
launch_app(){ export RTE_LOG_LEVEL=warning; if command -v setsid >/dev/null 2>&1; then setsid ./software-packet-distributor -l 2,3,4,5,6,7,8-15 --main-lcore 2 --file-prefix spd1 --huge-unlink & else ./software-packet-distributor -l 2,3,4,5,6,7,8-15 --main-lcore 2 --file-prefix spd1 --huge-unlink & fi; PID=$!; PGID=$(ps -o pgid= -p "$PID" 2>/dev/null | tr -d ' '); [ -z "$PGID" ] && PGID="$PID"; log "[start] software-packet-distributor started: pid=$PID pgid=$PGID"; trap 'log "[start] INT -> app"; kill -INT -"$PGID" 2>/dev/null || true' INT; sleep "$RUN_SECS" || true; log "[start] SIGINT app"; kill -INT -"$PGID" 2>/dev/null || true; for i in 1 2 3 4 5 6; do sleep 5 || true; if ! kill -0 "$PID" 2>/dev/null; then log "[start] app exited"; break; fi; done; alive(){ kill -0 "$PID" 2>/dev/null; }; if alive; then log "[start] still running after 30s; escalating to SIGTERM"; kill -TERM -"$PGID" 2>/dev/null || true; fi; for i in 1 2 3 4 5; do sleep 6 || true; if ! kill -0 "$PID" 2>/dev/null; then log "[start] app exited"; break; fi; done; if alive; then log "[start] still running after 60s; escalating to SIGKILL"; kill -KILL -"$PGID" 2>/dev/null || true; fi; cleanup_stale; }
log "[start] Reconciling HugePages configuration..."; ensure_mounts; ensure_counts; log "[start] HugePages_Total/Free:"; grep -E 'HugePages_(Total|Free)|Hugepagesize' /proc/meminfo || true; chmod +x ./software-packet-distributor || true; launch_app
