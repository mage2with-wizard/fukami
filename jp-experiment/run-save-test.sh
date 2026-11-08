#!/bin/bash
# Memory-card persistence test: recorded race-test input, but the card folder is
# fixed (RRV_GATE3_MC_PERSISTENT=1, workload binds storage persistent_user_card).
# Usage: run-save-test.sh <app-folder> <card-folder> <present-limit> [capture-fields] [workload]
set -eu
app=$1; card=$2; limit=$3; fields=${4:-}; workload=${5:-$(dirname "$0")/save-test-workload.txt}
run="$card/../runs/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$card" "$run/captures" "$run/config" "$run/cache"
export LD_LIBRARY_PATH="$app/lib"
export SDL_VIDEODRIVER=wayland
export XDG_CONFIG_HOME="$run/config" XDG_CACHE_HOME="$run/cache"
export RRV_BOOT_ARGV0='cdrom0:\SLPS_200.01;1'
export RRV_GATE3_BOUND_WORKLOAD="$workload"
export RRV_GATE3_MC_ROOT="$card" RRV_GATE3_MC_PERSISTENT=1
export RRV_GATE3_REALTIME=1 RRV_GATE3_LIVE_PAD=0
[ -n "${FF:-}" ] && export RRV_GATE3_FAST_FORWARD=$FF
export RRV_GS_EXECUTION=worker-sync RRV_VU1GS_EXECUTION=owner-async RRV_GS_RENDER_MODE=full RRV_PCSX2_GS_FULL_SCALE=${SCALE:-1}
export RRV_RR5_NATIVE_HOT=${NATIVE:-0} RRV_RR5_FAST_UNPACK=1 RRV_RR5_FAST_UNPACK_AHEAD=${AHEAD:-0}
[ -n "${SPLIT:-}" ] && export RRV_VU1GS_SPLIT=1
[ -n "$fields" ] && export RRV_GATE4_GS_CAPTURE_DIR="$run/captures" RRV_GATE4_GS_CAPTURE_FIELDS="$fields"
export RRV_GATE4_START_CLOCK="$run/start-clock.csv" RRV_TEST_PRESENT_LIMIT=$limit RRV_PHASE_LOG=1
echo "$run"
exec timeout --signal=TERM --kill-after=10s ${TIMEOUT:-600}s "$app/bin/Fukami" "${DISC:-$card/../disc}/SLPS_200.01" > "$run/runtime.log" 2>&1
