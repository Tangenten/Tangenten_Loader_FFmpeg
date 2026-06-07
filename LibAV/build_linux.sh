#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Output paths — override via TLAV_OUT_DIR, TLAV_*_NAME env vars.
: "${TLAV_OUT_DIR:=$script_dir/linux_x86_64}"
: "${TLAV_ABI3_NAME:=libtangenten_libav_bridge_abi3.so}"
: "${TLAV_ABI2_NAME:=libtangenten_libav_bridge_abi2.so}"
: "${TLAV_LEGACY_NAME:=libtangenten_libav_bridge.so}"

out_dir="$TLAV_OUT_DIR"
abi3_out="$out_dir/$TLAV_ABI3_NAME"
abi2_out="$out_dir/$TLAV_ABI2_NAME"
legacy_out="$out_dir/$TLAV_LEGACY_NAME"
mkdir -p "$out_dir"

cc -O2 -fPIC -shared -Wall -Wextra \
  $(pkg-config --cflags libavformat libavcodec libavutil libswscale 2>/dev/null || true) \
  "$script_dir/tangenten_libav_bridge.c" \
  -o "$abi3_out" \
  -ldl -lpthread

cp -f "$abi3_out" "$abi2_out"
cp -f "$abi3_out" "$legacy_out"

printf 'built %s\n' "$abi3_out"
printf 'updated %s\n' "$abi2_out"
printf 'updated %s\n' "$legacy_out"
