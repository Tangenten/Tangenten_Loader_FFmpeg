Tangenten Loader (FFmpeg)
=========================

Loader (FFmpeg) is a Fusion fuse that decodes video frames through a small
native LibAV bridge. The bridge uses FFmpeg/libav runtime libraries copied into
the fuse-local LibAV platform folder.

Install
-------

Copy this folder into your Fusion Fuses directory:

  Tangenten_Loader_FFmpeg/

The repo ships pre-built bridge binaries for all platforms in `LibAV/<platform>/`.
If you need to rebuild them (e.g. after updating the C source), run:

  cd LibAV
  ./build_linux.sh              # Linux   -> linux_x86_64/
  ./build_macos.sh              # macOS   -> mac_x86_64/, mac_arm64/
  build_windows.bat             # Windows -> windows_x86_64/ (MSVC, FFMPEG_DIR)

After building or obtaining a pre-built bridge, copy matching FFmpeg/libav
runtime libraries into the same platform folder. The fuse does not fall back
to system libraries.

Development builds (run each on its own platform; see LibAV/ReadMe.txt):

  cd Tangenten_Loader_FFmpeg/LibAV
  ./build_linux.sh                 # Linux   -> linux_x86_64/
  ./build_macos.sh                 # macOS   -> mac_x86_64/, mac_arm64/
  build_windows.bat                # Windows -> windows_x86_64/ (MSVC, FFMPEG_DIR)

