#!/usr/bin/env bash
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

if ! command -v pkg-config >/dev/null 2>&1; then
	echo "error: pkg-config not found (try: brew install pkg-config ffmpeg)" >&2
	exit 1
fi

cflags="$(pkg-config --cflags libavformat libavcodec libavutil libswscale 2>/dev/null || true)"

build_arch() {
	local arch="$1"
	local out_dir="$script_dir/mac_$arch"
	local abi3_out="$out_dir/libtangenten_libav_bridge_abi3.dylib"
	local abi2_out="$out_dir/libtangenten_libav_bridge_abi2.dylib"
	local legacy_out="$out_dir/libtangenten_libav_bridge.dylib"
	mkdir -p "$out_dir"

	cc -O2 -fPIC -dynamiclib -arch "$arch" -Wall -Wextra \
		$cflags \
		"$script_dir/tangenten_libav_bridge.c" \
		-o "$abi3_out"

	cp -f "$abi3_out" "$abi2_out"
	cp -f "$abi3_out" "$legacy_out"

	printf 'built %s\n' "$abi3_out"
	printf 'updated %s\n' "$abi2_out"
	printf 'updated %s\n' "$legacy_out"
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
