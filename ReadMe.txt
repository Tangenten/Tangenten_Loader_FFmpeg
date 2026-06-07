Tangenten Loader (FFmpeg)
=========================

Loader (FFmpeg) is a Fusion fuse that decodes video frames through a small
native LibAV bridge. The bridge uses FFmpeg/libav runtime libraries copied into
the fuse-local LibAV platform folder.

Install
-------

Copy this folder into your Fusion Fuses directory:

  Tangenten_Loader_FFmpeg/

You must compile the native bridge for your platform first. Run the
appropriate build script from the LibAV/ folder:

  cd LibAV
  ./build_linux.sh              # Linux   -> linux_x86_64/
  ./build_macos.sh              # macOS   -> mac_x86_64/, mac_arm64/
  build_windows.bat             # Windows -> windows_x86_64/ (MSVC, FFMPEG_DIR)

Each script requires FFmpeg development headers on your system
(see LibAV/ReadMe.txt for details).

After building, copy matching FFmpeg/libav runtime libraries into the same
platform folder. The fuse does not fall back to system libraries.

License
-------

This project is licensed under LGPL-2.1-or-later. See License.txt for full
terms and the FFmpeg/libav compliance notice.

In short:
- The bridge (libav_bridge.c) includes FFmpeg headers at compile time but
  does NOT link against or bundle FFmpeg runtime binaries.
- The Lua scripts (Loader_FFmpeg.fuse, LibAV_FFI.lua) never touch FFmpeg
  directly; they are independent LGPL software.

