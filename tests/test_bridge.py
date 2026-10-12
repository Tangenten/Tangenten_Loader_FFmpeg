#!/usr/bin/env python3
"""Smoke-test the LibAV bridge against a generated video clip."""

import ctypes
import subprocess
import sys
import tempfile
from pathlib import Path


class TlavInfo(ctypes.Structure):
    _fields_ = [
        ("width", ctypes.c_int),
        ("height", ctypes.c_int),
        ("fps", ctypes.c_double),
        ("frame_count", ctypes.c_int64),
        ("duration", ctypes.c_double),
        ("codec_name", ctypes.c_char * 64),
        ("pixel_format", ctypes.c_char * 64),
        ("format_name", ctypes.c_char * 128),
    ]


class TlavFrame(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.POINTER(ctypes.c_uint8)),
        ("width", ctypes.c_int),
        ("height", ctypes.c_int),
        ("stride", ctypes.c_int),
        ("frame_index", ctypes.c_int64),
    ]


def error_text(bridge):
    value = bridge.tlav_last_error()
    return value.decode("utf-8", "replace") if value else "unknown error"


def main():
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} BRIDGE_PATH FFMPEG_LIBRARY_DIR")

    bridge_path = Path(sys.argv[1]).resolve()
    library_dir = Path(sys.argv[2]).resolve()
    bridge = ctypes.CDLL(str(bridge_path))
    bridge.tlav_set_library_dir.argtypes = [ctypes.c_char_p]
    bridge.tlav_last_error.restype = ctypes.c_char_p
    bridge.tlav_open.argtypes = [ctypes.c_char_p, ctypes.POINTER(TlavInfo)]
    bridge.tlav_open.restype = ctypes.c_void_p
    bridge.tlav_close.argtypes = [ctypes.c_void_p]
    bridge.tlav_decode_into_scaled.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int64,
        ctypes.POINTER(ctypes.c_uint8),
        ctypes.c_int,
        ctypes.POINTER(TlavFrame),
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
    ]
    bridge.tlav_decode_into_scaled.restype = ctypes.c_int

    bridge.tlav_set_library_dir(str(library_dir).encode("utf-8"))
    with tempfile.TemporaryDirectory() as directory:
        clip_path = Path(directory) / "test.mkv"
        subprocess.run(
            [
                "ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i",
                "color=c=red:size=16x8:rate=2", "-frames:v", "3", "-c:v", "ffv1", str(clip_path),
            ],
            check=True,
        )
        info = TlavInfo()
        handle = bridge.tlav_open(str(clip_path).encode("utf-8"), ctypes.byref(info))
        if not handle:
            raise AssertionError(f"open failed: {error_text(bridge)}")
        try:
            assert (info.width, info.height) == (16, 8)
            assert info.fps == 2.0
            assert info.frame_count >= 3
            pixels = (ctypes.c_uint8 * (16 * 8 * 4))()
            frame = TlavFrame()
            decoded = bridge.tlav_decode_into_scaled(handle, 0, pixels, 16 * 4, ctypes.byref(frame), 0, 16, 8)
            if decoded != 1:
                raise AssertionError(f"decode failed: {error_text(bridge)}")
            assert (frame.width, frame.height, frame.stride, frame.frame_index) == (16, 8, 64, 0)
            assert any(pixels)
        finally:
            bridge.tlav_close(handle)

    print("LibAV bridge smoke test passed")


if __name__ == "__main__":
    main()
