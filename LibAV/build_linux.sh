#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out_dir="$script_dir/linux_x86_64"
abi_out="$out_dir/libtangenten_libav_bridge_abi3.so"
abi2_out="$out_dir/libtangenten_libav_bridge_abi2.so"
legacy_out="$out_dir/libtangenten_libav_bridge.so"
mkdir -p "$out_dir"

cc -O2 -fPIC -shared -Wall -Wextra \
  $(pkg-config --cflags libavformat libavcodec libavutil libswscale 2>/dev/null || true) \
  "$script_dir/tangenten_libav_bridge.c" \
  -o "$abi_out" \
  -ldl -lpthread

cp -f "$abi_out" "$abi2_out"
cp -f "$abi_out" "$legacy_out"

printf 'built %s\n' "$abi_out"
printf 'updated %s\n' "$abi2_out"
printf 'updated %s\n' "$legacy_out"
