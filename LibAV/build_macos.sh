# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (c) 2026 Tangenten.
#
# Build the Tangenten LibAV bridge for macOS.
#
# Produces fat-free per-arch dylibs in the platform folders the fuse looks in:
#   mac_x86_64/  (Intel)        mac_arm64/  (Apple Silicon)
#
# Only FFmpeg/libav *headers* are needed at build time; the bridge loads the
# runtime dylibs dynamically, so nothing is linked against libav here. Install
# the headers with Homebrew (`brew install ffmpeg`) so pkg-config can find them.
#
# Usage:
#   ./build_macos.sh            # build both x86_64 and arm64
#   ./build_macos.sh x86_64     # build a single arch
#   ./build_macos.sh arm64

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Output path — override via TLAV_OUT_DIR_TEMPLATE env var.
# TLAV_OUT_DIR_TEMPLATE uses %a as the arch placeholder (e.g. mac_%a -> mac_x86_64).
: "${TLAV_OUT_DIR_TEMPLATE:=$script_dir/mac_%a}"

if ! command -v pkg-config >/dev/null 2>&1; then
	echo "error: pkg-config not found (try: brew install pkg-config ffmpeg)" >&2
	exit 1
fi

cflags="$(pkg-config --cflags libavformat libavcodec libavutil libswscale 2>/dev/null || true)"

build_arch() {
	local arch="$1"
	local out_dir="$(echo "$TLAV_OUT_DIR_TEMPLATE" | sed "s/%a/$arch/g")"
	local out_path="$out_dir/libav_bridge.dylib"
	mkdir -p "$out_dir"

	cc -O2 -fPIC -dynamiclib -arch "$arch" -Wall -Wextra \
		$cflags \
		"$script_dir/tangenten_libav_bridge.c" \
		-o "$out_path"

	printf 'built %s\n' "$out_path"
}

case "${1:-both}" in
	both)
		build_arch x86_64
		build_arch arm64
		;;
	x86_64 | arm64)
		build_arch "$1"
		;;
	*)
		echo "usage: $0 [both|x86_64|arm64]" >&2
		exit 1
		;;
esac
