#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (c) 2026 Tangenten.
#
# Build the Tangenten LibAV bridge for Linux (x86_64).

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Output path — override via TLAV_OUT_DIR env var.
: "${TLAV_OUT_DIR:=$script_dir/linux_x86_64}"
# Build ID embedded in the binary so every CI run produces unique output.
: "${TLAV_BUILD_ID:=dev}"

out_dir="$TLAV_OUT_DIR"
out_path="$out_dir/libav_bridge.so"
mkdir -p "$out_dir"

cflags="$(pkg-config --cflags libavformat libavcodec libavutil libswscale)"
tmp_path="$(mktemp "$out_dir/.libav_bridge.XXXXXX")"
trap 'rm -f "$tmp_path"' EXIT

cc -O2 -fPIC -shared -Wall -Wextra \
  $cflags \
  -DTLAV_BUILD_ID=\"$TLAV_BUILD_ID\" \
  "$script_dir/libav_bridge.c" \
  -o "$tmp_path" \
  -ldl -lpthread

chmod 755 "$tmp_path"
mv -f "$tmp_path" "$out_path"

printf 'built %s\n' "$out_path"
