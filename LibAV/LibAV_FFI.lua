-- SPDX-License-Identifier: LGPL-2.1-or-later
--
-- Copyright (c) 2026 Tangenten.
--
-- Loader_FFmpeg LuaJIT FFI bindings for the Tangenten LibAV bridge.
-- The bridge and FFmpeg/libav runtime libraries are loaded only from the
-- fuse-local LibAV/<platform> folder.

local ffi = require("ffi")

_G.__TANGENTEN_LIBAV_FFI = nil

local cdefOk, cdefErr

if not _G.__TANGENTEN_LIBAV_CDEF_DONE then
	cdefOk, cdefErr = pcall(ffi.cdef, [[
		typedef struct TlavInfo {
			int width;
			int height;
			double fps;
			int64_t frame_count;
			double duration;
			char codec_name[64];
			char pixel_format[64];
			char format_name[128];
		} TlavInfo;

		typedef struct TlavFrame {
			const uint8_t* data;
			int width;
			int height;
			int stride;
			int64_t frame_index;
		} TlavFrame;

		void tlav_set_library_dir(const char* dir);
		const char* tlav_version(void);
		const char* tlav_last_error(void);
		const char* tlav_last_debug(void);
		void* tlav_open(const char* path, TlavInfo* info);
		int tlav_decode(void* handle, int64_t frame_index, TlavFrame* frame);
		int tlav_decode_into(void* handle, int64_t frame_index, uint8_t* dst_buffer, int dst_line_stride, TlavFrame* frame);
		int tlav_decode_into_format(void* handle, int64_t frame_index, uint8_t* dst_buffer, int dst_line_stride, TlavFrame* frame, int output_format);
		void tlav_close(void* handle);
		const char* tlav_hw_accel(void* handle);
	]])

	if not cdefOk then
		_G.__TANGENTEN_LIBAV_CDEF_ERROR = cdefErr
		return nil, "LibAV FFI cdef failed: " .. tostring(cdefErr)
	end

	_G.__TANGENTEN_LIBAV_CDEF_DONE = true
end

pcall(ffi.cdef, [[
	void* malloc(size_t size);
	void free(void* ptr);
]])

if _G.__TANGENTEN_LIBAV_CDEF_ERROR then
	return nil, _G.__TANGENTEN_LIBAV_CDEF_ERROR
end

pcall(ffi.cdef, [[
	int tlav_decode_into(void* handle, int64_t frame_index, uint8_t* dst_buffer, int dst_line_stride, TlavFrame* frame);
	int tlav_decode_into_format(void* handle, int64_t frame_index, uint8_t* dst_buffer, int dst_line_stride, TlavFrame* frame, int output_format);
]])

local function getPathSeparator()
	return package.config:sub(1, 1)
end

local function platformDir()
	local osName
	if ffi.os == "Windows" then
		osName = "windows"
	elseif ffi.os == "OSX" then
		osName = "mac"
	else
		osName = "linux"
	end

	local archName
	if ffi.arch == "x64" then
		archName = "x86_64"
	elseif ffi.arch == "arm64" then
		archName = "arm64"
	elseif ffi.arch == "x86" then
		archName = "x86"
	else
		archName = tostring(ffi.arch)
	end

	return osName .. "_" .. archName
end

local function bridgeFileName()
	if ffi.os == "Windows" then
		return "libav_bridge.dll"
	elseif ffi.os == "OSX" then
		return "libav_bridge.dylib"
	end

	return "libav_bridge.so"
end

local function fileExists(path)
	local f = io.open(path, "rb")
	if f then
		f:close()
		return true
	end

	return false
end

local function loadLibrary(fuseDir)
	local sep = getPathSeparator()
	local libDir = fuseDir .. sep .. "LibAV" .. sep .. platformDir()
	local bridgeName = bridgeFileName()
	local bridgePath = libDir .. sep .. bridgeName
	if not fileExists(bridgePath) then
		return nil, "LibAV bridge not found at " .. bridgePath
	end

	local ok, lib = pcall(ffi.load, bridgePath)
	if not ok then
		return nil, "Failed to load LibAV bridge from '" .. bridgePath .. "': " .. tostring(lib)
	end

	lib.tlav_set_library_dir(libDir)
	return lib, nil, libDir, bridgePath, bridgeName
end

local function ffiString(value)
	if value == nil then return "" end
	return ffi.string(value)
end

local function infoToTable(info)
	return {
		width = tonumber(info.width) or 0,
		height = tonumber(info.height) or 0,
		fps = tonumber(info.fps) or 0,
		frameCount = tonumber(info.frame_count) or 0,
		duration = tonumber(info.duration) or 0,
		codecName = ffiString(info.codec_name),
		pixelFormat = ffiString(info.pixel_format),
		formatName = ffiString(info.format_name),
	}
end

local function build(fuseDir)
	local C, err, libDir, bridgePath, bridgeName = loadLibrary(fuseDir)
	if not C then
		return nil, err
	end

	local module = {}
	module.C = C
	module.ffi = ffi
	module.libDir = libDir
	module.bridgePath = bridgePath
	module.bridgeName = bridgeName

	local function loadedC()
		if module.C == nil then
			error("LibAV bridge handle is not loaded", 2)
		end

		return module.C
	end

	local lastDebugFn
	local hasLastDebug = pcall(function()
		lastDebugFn = loadedC().tlav_last_debug
	end)

	function module.version()
		return ffiString(loadedC().tlav_version())
	end

	function module.lastError()
		return ffiString(loadedC().tlav_last_error())
	end

	if hasLastDebug and lastDebugFn then
		function module.lastDebug()
			return ffiString(lastDebugFn())
		end
	else
		function module.lastDebug()
			return ""
		end
	end

	function module.open(path)
		local info = ffi.new("TlavInfo")
		local handle = loadedC().tlav_open(path, info)
		if handle == nil or handle == ffi.NULL then
			return nil, module.lastError()
		end

		local decoder = {
			handle = ffi.gc(handle, loadedC().tlav_close),
			info = infoToTable(info),
		}

		-- Additive export; tolerate an older bridge/cdef that lacks it.
		local hwOk, hwName = pcall(function()
			return ffiString(loadedC().tlav_hw_accel(handle))
		end)
		decoder.info.hwAccel = (hwOk and hwName) or ""

		local decodeIntoOk, decodeIntoFn = pcall(function()
			return loadedC().tlav_decode_into
		end)
		local decodeIntoFormatOk, decodeIntoFormatFn = pcall(function()
			return loadedC().tlav_decode_into_format
		end)
		if decodeIntoOk and decodeIntoFn then
			function decoder:decodeInto(frameIndex, dstBuffer, dstLineStride)
				local frame = ffi.new("TlavFrame")
				local ok = decodeIntoFn(self.handle, frameIndex, ffi.cast("uint8_t*", dstBuffer), dstLineStride, frame)
				if ok == 0 then
					return nil, module.lastError()
				end

				return frame
			end
		end
		if decodeIntoFormatOk and decodeIntoFormatFn then
			function decoder:decodeIntoFormat(frameIndex, dstBuffer, dstLineStride, outputFormat)
				local frame = ffi.new("TlavFrame")
				local ok = decodeIntoFormatFn(self.handle, frameIndex, ffi.cast("uint8_t*", dstBuffer), dstLineStride, frame, outputFormat or 0)
				if ok == 0 then
					return nil, module.lastError()
				end

				return frame
			end
		end

		function decoder:decode(frameIndex)
			local frame = ffi.new("TlavFrame")
			local ok = loadedC().tlav_decode(self.handle, frameIndex, frame)
			if ok == 0 then
				return nil, module.lastError()
			end

			return frame
		end

		function decoder:close()
			if self.handle ~= nil and self.handle ~= ffi.NULL then
				ffi.gc(self.handle, nil)
				loadedC().tlav_close(self.handle)
				self.handle = nil
			end
		end

		return decoder
	end

	_G.__TANGENTEN_LIBAV_FFI = module
	return module
end

return build
