@echo off
setlocal enabledelayedexpansion
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
rem Output paths — override via TLAV_OUT_DIR, TLAV_*_NAME env vars.
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
if not defined TLAV_ABI3_NAME set "TLAV_ABI3_NAME=tangenten_libav_bridge_abi3.dll"
if not defined TLAV_ABI2_NAME set "TLAV_ABI2_NAME=tangenten_libav_bridge_abi2.dll"
if not defined TLAV_LEGACY_NAME set "TLAV_LEGACY_NAME=tangenten_libav_bridge.dll"

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

set "abi3=%out_dir%\%TLAV_ABI3_NAME%"
set "abi2=%out_dir%\%TLAV_ABI2_NAME%"
set "legacy=%out_dir%\%TLAV_LEGACY_NAME%"

cl /nologo /O2 /MD /W3 /TC /std:c11 ^
	/I"%FFMPEG_DIR%\include" ^
	/LD "%script_dir%tangenten_libav_bridge.c" ^
	/Fo:"%out_dir%\tangenten_libav_bridge.obj" ^
	/Fe:"%abi3%"
if errorlevel 1 exit /b 1

copy /y "%abi3%" "%abi2%" >nul
copy /y "%abi3%" "%legacy%" >nul

rem Drop MSVC intermediates, keep only the DLLs.
del /q "%out_dir%\tangenten_libav_bridge.obj" 2>nul
del /q "%out_dir%\tangenten_libav_bridge_abi3.lib" 2>nul
del /q "%out_dir%\tangenten_libav_bridge_abi3.exp" 2>nul

echo built %abi3%
echo updated %abi2%
echo updated %legacy%
