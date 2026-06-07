# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (c) 2026 Tangenten.
#
# Build the Tangenten LibAV bridge for Linux (x86_64).

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Output path — override via TLAV_OUT_DIR env var.
: "${TLAV_OUT_DIR:=$script_dir/linux_x86_64}"

out_dir="$TLAV_OUT_DIR"
out_path="$out_dir/libav_bridge.so"
mkdir -p "$out_dir"

cc -O2 -fPIC -shared -Wall -Wextra \
  $(pkg-config --cflags libavformat libavcodec libavutil libswscale 2>/dev/null || true) \
  "$script_dir/tangenten_libav_bridge.c" \
  -o "$out_path" \
  -ldl -lpthread

printf 'built %s\n' "$out_path"
