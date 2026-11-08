#!/bin/bash
set -eu
root=/tmp/fukami-jp-source
run="$root/jp-run/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$run/mc" "$run/captures" "$run/config/PCSX2" "$run/cache"
export LD_LIBRARY_PATH="${FUKAMI_NA_APP:-$HOME/Applications/Fukami}/lib"
export SDL_VIDEODRIVER=wayland
export RRV_BOOT_ARGV0="cdrom0:\SLPS_200.01;1"
export XDG_CONFIG_HOME="$run/config"
export XDG_CACHE_HOME="$run/cache"
export RRV_GATE3_BOUND_WORKLOAD="$root/jp-experiment/bound-workload.txt"
export RRV_GATE3_MC_ROOT="$run/mc"
export RRV_GATE3_MC_PERSISTENT=0
export RRV_GATE3_REALTIME=1
export RRV_GATE3_LIVE_PAD=1
export RRV_GS_EXECUTION=worker-sync
export RRV_VU1GS_EXECUTION=owner-async
export RRV_GS_RENDER_MODE=full
export RRV_PCSX2_GS_FULL_SCALE=1
export RRV_RR5_NATIVE_HOT=0
export RRV_RR5_FAST_UNPACK=1
export RRV_RR5_FAST_UNPACK_AHEAD=0
export RRV_GATE4_GS_CAPTURE_DIR="$run/captures"
export RRV_GATE4_GS_CAPTURE_FIELDS=60,120,300,600,900,1200,1800,2400,3000
export RRV_GATE4_START_CLOCK="$run/start-clock.csv"
export RRV_TEST_PRESENT_LIMIT=3000
export RRV_PHASE_LOG=1
printf '%s\n' "$run" > "$root/jp-run/latest-run.txt"
exec timeout --signal=TERM --kill-after=10s 180s "$root/jp-build/bin/Fukami" "$root/jp-run/disc/SLPS_200.01" > "$run/runtime.log" 2>&1
