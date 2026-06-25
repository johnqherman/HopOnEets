#!/usr/bin/env bash
# resim-runner.sh - headless authoritative re-sim of one input log (spec Part 6; docs/headless-resim.md).
#
# Runs the canonical Eets build with the Hop On Eets mod in re-sim mode, windowless: SDL dummy audio +
# a virtual framebuffer (xvfb) for the GL context the FNA3D backend needs. The mod loads the log,
# applies the build, force-starts, reads the outcome, writes Log/hop_on_eets_verdict.json, and exits
# (HOE_RESIM_EXIT=1) with: 0 = re-sim reproduced the claim, 1 = did not, 124 = timeout/hang.
#
# Usage:  resim-runner.sh <input-log.json> [-d GAME_DIR] [-l LEVEL_INDEX] [-t TIMEOUT_SECONDS]
# Env:    EETS_DIR (game dir), EETS_BIN (game executable), EETSMOD_LOADER (LD_PRELOAD .so if not installed)
#
# Prints the verdict JSON to stdout; exits with the mod's code. Pair with netproto/verifier.ts.
set -euo pipefail

log_in=""; game_dir="${EETS_DIR:-}"; level=""; timeout_s=180
while [[ $# -gt 0 ]]; do
  case "$1" in
    -d) game_dir="$2"; shift 2;;
    -l) level="$2"; shift 2;;
    -t) timeout_s="$2"; shift 2;;
    -*) echo "unknown flag: $1" >&2; exit 2;;
    *)  log_in="$1"; shift;;
  esac
done
[[ -n "$log_in" && -f "$log_in" ]] || { echo "usage: resim-runner.sh <input-log.json> [-d GAME_DIR] [-l LEVEL] [-t SECS]" >&2; exit 2; }
log_abs="$(cd "$(dirname "$log_in")" && pwd)/$(basename "$log_in")"

# locate the game dir + executable
[[ -n "$game_dir" && -d "$game_dir" ]] || { echo "set EETS_DIR or pass -d GAME_DIR (the Eets install dir)" >&2; exit 2; }
game_bin="${EETS_BIN:-}"
if [[ -z "$game_bin" ]]; then
  for c in "$game_dir/Eets" "$game_dir/eets" "$game_dir/Eets.x86_64" "$game_dir/Eets.bin.x86_64"; do
    [[ -x "$c" ]] && { game_bin="$c"; break; }
  done
fi
[[ -n "$game_bin" && -x "$game_bin" ]] || { echo "could not find the Eets executable in $game_dir; set EETS_BIN" >&2; exit 2; }

verdict="$game_dir/Log/hop_on_eets_verdict.json"
rm -f "$verdict"                      # drop any stale verdict so we only read this run's

# env: per-run re-sim parameters + headless backends
export HOE_RESIM_FILE="$log_abs" HOE_RESIM_EXIT=1
[[ -n "$level" ]] && export HOE_RESIM_LEVEL="$level"
export SDL_AUDIODRIVER=dummy          # no audio device needed
# the native loader (libeetsmod.so) is LD_PRELOADed at runtime; the user's normal (Steam) launch does this,
# so a headless run must too or only the Lua mods load and the re-sim never engages. Default to the loader
# shipped in the game dir; override with EETSMOD_LOADER.
loader_so="${EETSMOD_LOADER:-$game_dir/libeetsmod.so}"
[[ -f "$loader_so" ]] && export LD_PRELOAD="${loader_so}${LD_PRELOAD:+:$LD_PRELOAD}"
[[ -f "$loader_so" ]] || echo "warning: native loader not found ($loader_so); set EETSMOD_LOADER - else the mod won't load" >&2

runner=()
if [[ -z "${DISPLAY:-}" ]]; then
  # xvfb path (validated default): real GL backend under a virtual framebuffer. The screen MUST advertise
  # GLX (+extension GLX +render) or FNA3D's GL context creation fails silently and the game exits before the
  # menu - a bare `xvfb-run -a` (no GLX) does exactly that.
  if command -v xvfb-run >/dev/null; then runner=(xvfb-run -a -s "-screen 0 1024x768x24 +extension GLX +render -noreset")
  else echo "warning: no DISPLAY and no xvfb-run; the GL backend may fail. Install xorg-server-xvfb or run under :0." >&2; fi
fi

echo "resim: $game_bin  log=$log_abs  level=${level:-from-log}  timeout=${timeout_s}s" >&2
set +e
( cd "$game_dir" && exec "${runner[@]}" timeout --signal=KILL "$timeout_s" "$game_bin" ) >/dev/null 2>&1
code=$?
set -e
[[ $code -eq 137 || $code -eq 124 ]] && { echo "resim: TIMEOUT after ${timeout_s}s (no verdict)" >&2; code=124; }

if [[ -f "$verdict" ]]; then
  cat "$verdict"
else
  echo '{"reproduced":false,"error":"no verdict written (game crashed / never reached the level)"}'
  [[ $code -eq 0 ]] && code=1
fi
exit $code
