@echo off
setlocal enabledelayedexpansion
rem SPDX-License-Identifier: LGPL-2.1-or-later
rem Copyright (c) 2026 Tangenten.
rem
rem Build the Tangenten LibAV bridge for Windows (x64) with MSVC.
rem
rem Run this from a "x64 Native Tools Command Prompt for VS" so cl.exe is on
rem PATH. Only FFmpeg/libav *headers* are needed; the bridge loads the runtime
rem DLLs dynamically, so it is not linked against libav. Point FFMPEG_DIR at an
rem FFmpeg dev package that contains an "include" folder (e.g. a gyan.dev
rem "shared" or "dev" build):
rem
rem   set FFMPEG_DIR=C:\ffmpeg
rem   build_windows.bat
rem
rem Output path — override via TLAV_OUT_DIR env var.
rem   set TLAV_OUT_DIR=some\other\path
rem   build_windows.bat
rem
rem Default output: windows_x86_64\.

set "script_dir=%~dp0"
if defined TLAV_OUT_DIR (
	set "out_dir=%TLAV_OUT_DIR%"
) else (
	set "out_dir=%script_dir%windows_x86_64"
)

if not defined FFMPEG_DIR (
	echo error: set FFMPEG_DIR to your FFmpeg dev folder ^(must contain include\^)
	exit /b 1
)
if not exist "%FFMPEG_DIR%\include\libavcodec\avcodec.h" (
	echo error: "%FFMPEG_DIR%\include\libavcodec\avcodec.h" not found
	echo        FFMPEG_DIR must point at a dev package with FFmpeg headers
	exit /b 1
)
where cl >nul 2>nul
if errorlevel 1 (
	echo error: cl.exe not found - run from a "x64 Native Tools Command Prompt for VS"
	exit /b 1
)

if not exist "%out_dir%" mkdir "%out_dir%"

set "out_path=%out_dir%\libav_bridge.dll"

cl /nologo /O2 /MD /W3 /TC /std:c11 ^
	/I"%FFMPEG_DIR%\include" ^
	/LD "%script_dir%tangenten_libav_bridge.c" ^
	/Fo:"%out_dir%\libav_bridge.obj" ^
	/Fe:"%out_path%"
if errorlevel 1 exit /b 1

rem Drop MSVC intermediates, keep only the DLL.
del /q "%out_dir%\libav_bridge.obj" 2>nul
del /q "%out_dir%\libav_bridge.exp" 2>nul
del /q "%out_dir%\libav_bridge.lib" 2>nul

echo built %out_path%
