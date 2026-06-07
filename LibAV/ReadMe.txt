Tangenten Loader LibAV bridge
=============================

SPDX-License-Identifier: LGPL-2.1-or-later
Copyright (c) 2026 Tangenten.

This folder contains the native bridge used by Loader (FFmpeg). The fuse
loads the bridge and FFmpeg/libav runtime libraries only from:

  LibAV/<platform>/

Known platform folders:

  linux_x86_64
  windows_x86_64
  mac_x86_64
  mac_arm64

The bridge does not bundle FFmpeg/libav. Copy the matching runtime libraries
for your platform into the same folder as the bridge.

Build scripts (each must run on its own platform; only FFmpeg/libav headers are
needed at build time, never the import/runtime libraries):

  build_linux.sh              -> linux_x86_64/   (needs pkg-config + ffmpeg dev)
  build_macos.sh [arch]       -> mac_x86_64/, mac_arm64/   (clang; brew ffmpeg)
  build_windows.bat           -> windows_x86_64/  (MSVC; set FFMPEG_DIR)

build_macos.sh builds both Intel and Apple Silicon by default; pass `x86_64` or
`arm64` to build a single slice. build_windows.bat runs from an "x64 Native
Tools Command Prompt for VS" with FFMPEG_DIR pointing at an FFmpeg dev package
that has an include\ folder. Each script writes a single bridge file for its
platform.

Linux example runtime files:

  libtangenten_libav_bridge.so
  libavformat.so.62
  libavcodec.so.62
  libavutil.so.60
  libswscale.so.9
  libswresample.so.6


