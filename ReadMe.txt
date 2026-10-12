Tangenten Loader (FFmpeg)
=========================

Loader (FFmpeg) is a Fusion fuse that decodes video frames through a small
native LibAV bridge. The bridge uses FFmpeg/libav runtime libraries copied into
the fuse-local LibAV platform folder.

Install
-------

Copy this folder into your Fusion Fuses directory:
  Tangenten_Loader_FFmpeg/

The repo includes pre-built bridge binaries for all platforms in
LibAV/<platform>/. These are automatically rebuilt by CI on each push.

After cloning, copy matching FFmpeg/libav runtime libraries into each
platform folder. The fuse does not fall back to system libraries.

If you need to rebuild the bridge yourself (e.g. after modifying the C
source), run the appropriate build script from the LibAV/ folder:

  cd LibAV
  ./build_linux.sh              # Linux   -> linux_x86_64/
  ./build_macos.sh              # macOS   -> mac_x86_64/, mac_arm64/
  build_windows.bat             # Windows -> windows_x86_64/ (MSVC, FFMPEG_DIR)

License
-------

This project is licensed under LGPL-2.1-or-later. See License.txt for full
terms and the FFmpeg/libav compliance notice.

In short:
- The bridge (libav_bridge.c) includes FFmpeg headers at compile time but
  does NOT link against or bundle FFmpeg runtime binaries.
- The Lua scripts (Loader_FFmpeg.fuse, LibAV_FFI.lua) never touch FFmpeg
  directly; they are independent LGPL software.

---
Repository: https://github.com/Tangenten/Tangenten_Loader_FFmpeg

