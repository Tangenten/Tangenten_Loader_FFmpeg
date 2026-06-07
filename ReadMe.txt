Tangenten Loader (FFmpeg)
=========================

Loader (FFmpeg) is a Fusion fuse that decodes video frames through a small
native LibAV bridge. The bridge uses FFmpeg/libav runtime libraries copied into
the fuse-local LibAV platform folder.

Install
-------

Copy this folder into your Fusion Fuses directory:

  Tangenten_Loader_FFmpeg/

Build or copy the native bridge for your platform into:

  LibAV/<platform>/

Then copy matching FFmpeg/libav runtime libraries into that same folder. The
fuse does not fall back to system libraries.

Development builds (run each on its own platform; see LibAV/ReadMe.txt):

  cd Tangenten_Loader_FFmpeg/LibAV
  ./build_linux.sh                 # Linux   -> linux_x86_64/
  ./build_macos.sh                 # macOS   -> mac_x86_64/, mac_arm64/
  build_windows.bat                # Windows -> windows_x86_64/ (MSVC, FFMPEG_DIR)

Controls
--------

Video Path:
  The video file to decode. Fusion path maps are resolved before filesystem
  access.

Metadata:
  A read-only status label showing resolution, frame rate, frame count,
  duration, codec, pixel format, and load/decode errors.

Playback Mode:
  Auto maps the comp timeline to source frames. Manual uses the Frame slider.

Playback FPS:
  In Auto mode, 0 uses the source FPS. Any positive value overrides playback
  speed.

Loop Behaviour:
  Loop, Ping Pong, Hold Last, or Blank after the selected range ends.

Notes
-----

v1 decodes video only. hardware decoding are not exposed.
