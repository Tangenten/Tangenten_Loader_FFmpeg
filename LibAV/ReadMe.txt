Tangenten Loader LibAV bridge
=============================

SPDX-License-Identifier: LGPL-2.1-or-later
Copyright (c) 2026 Tangenten.

This folder contains the native bridge used by Loader (FFmpeg). You must
compile the bridge for your platform using one of the build scripts below.
The fuse loads the bridge and FFmpeg/libav runtime libraries only from:

  LibAV/<platform>/

Platform folders:

  linux_x86_64
  windows_x86_64
  mac_x86_64
  mac_arm64

The bridge does not bundle FFmpeg/libav. Copy the matching runtime libraries
for your platform into the same folder as the bridge. The runtime libraries
MUST match the FFmpeg major version the bridge was built against -- see
"FFmpeg version requirement" below.

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

FFmpeg version requirement
--------------------------

The bridge reads FFmpeg data structures directly, so the runtime libraries you
copy in MUST share the same major (soname) version the bridge was compiled
against. The official/CI bridge binaries are built against the latest FFmpeg
release (currently FFmpeg 8.x). Supplying a different major version (for example
FFmpeg 7.x) reads struct fields at the wrong offsets and shows up as wrong frame
sizes (e.g. a 1-pixel-wide image) and failed decodes.

If the versions do not match, the tool refuses to decode and shows an
"FFmpeg ABI mismatch" message in the node's status panel listing the major
versions it found and the ones it needs. Install runtime libraries with those
majors and reload the node.

Required sonames for an FFmpeg 8.x bridge:

  Linux:    libavutil.so.60   libavcodec.so.62   libavformat.so.62
            libswscale.so.9   libswresample.so.6
  macOS:    libavutil.60.dylib   libavcodec.62.dylib   libavformat.62.dylib
            libswscale.9.dylib   libswresample.6.dylib
  Windows:  avutil-60.dll   avcodec-62.dll   avformat-62.dll
            swscale-9.dll   swresample-6.dll

Get matching shared builds from your distribution (when it ships that FFmpeg
release), from the builds linked at ffmpeg.org, or by compiling FFmpeg yourself
with --enable-shared. On Linux the build folder also accepts an unversioned
fallback name (libavcodec.so, ...); the major it points to must still match.


