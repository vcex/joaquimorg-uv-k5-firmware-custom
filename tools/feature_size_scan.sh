#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${ROOT}/feature_size_scan
mkdir -p "$OUT/logs"
CSV=$OUT/feature_size_scan.csv
echo "flag,default,test_value,flash_bytes,flash_percent" > "$CSV"
cd "$ROOT"

# extract ENABLE_* flags and their defaults
mapfile -t flags < <(grep -E '^[[:space:]]*ENABLE_[A-Z0-9_]+[[:space:]]*\?=' Makefile | sed -E 's/^[[:space:]]*([A-Z0-9_]+)[[:space:]]*\?=.*/\1/')

echo "Found ${#flags[@]} flags to test"

for flag in "${flags[@]}"; do
  # get default (0/1), if not 0/1 treat as empty and skip
  default=$(grep -E "^[[:space:]]*${flag}[[:space:]]*\?=[[:space:]]*[0-9]" Makefile | sed -E 's/.*\?=[[:space:]]*([0-9]).*/\1/') || default=""
  if [ -z "$default" ]; then
    # skip flags without simple numeric default
    echo "Skipping $flag (no simple numeric default)"
    continue
  fi
  testval=$((1 - default))
  echo "\n=== Testing $flag: default=$default -> test=$testval ==="

  LOG="$OUT/logs/build_${flag}_${testval}.log"
  # clean then build with POCSAG enabled
  make clean >/dev/null 2>&1 || true
  # run build - we set ENABLE_POCSAG_SEND=1 explicitly
  if ! make ENABLE_POCSAG_SEND=1 ${flag}=${testval} V=1 2>&1 | tee "$LOG"; then
    echo "Build failed for $flag=$testval, recording failure"
    echo "$flag,$default,$testval,BUILD_FAILED,BUILD_FAILED" >> "$CSV"
    continue
  fi

  # extract FLASH usage from log
  flash_line=$(grep -E "^\s*FLASH:" "$LOG" || true)
  if [ -z "$flash_line" ]; then
    # try to find 'Memory region' section
    flash_line=$(grep -A2 "Memory region" "$LOG" | grep FLASH || true)
  fi
  if [ -z "$flash_line" ]; then
    echo "No FLASH line for $flag, marking as UNKNOWN"
    echo "$flag,$default,$testval,UNKNOWN,UNKNOWN" >> "$CSV"
  else
    # expected format: FLASH:       50156 B        60 KB     81.63%
    flash_bytes=$(echo "$flash_line" | awk '{print $2}' | tr -d ',')
    flash_percent=$(echo "$flash_line" | awk '{print $5}')
    echo "$flag,$default,$testval,$flash_bytes,$flash_percent" >> "$CSV"
    echo "Recorded: $flag -> $flash_bytes bytes ($flash_percent)"
  fi

done

# final baseline build (with defaults + POCSAG enabled) to compare
make clean >/dev/null 2>&1 || true
LOG="$OUT/logs/build_baseline.log"
make ENABLE_POCSAG_SEND=1 V=1 2>&1 | tee "$LOG" || true
flash_line=$(grep -E "^\s*FLASH:" "$LOG" || true)
if [ -n "$flash_line" ]; then
  flash_bytes=$(echo "$flash_line" | awk '{print $2}' | tr -d ',')
  flash_percent=$(echo "$flash_line" | awk '{print $5}')
  echo "BASELINE, , ,$flash_bytes,$flash_percent" >> "$CSV"
fi

echo "Scan complete. Results: $CSV" 
