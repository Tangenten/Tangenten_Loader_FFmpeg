/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 Tangenten.
 *
 * Tangenten LibAV bridge for Loader_FFmpeg.
 *
 * The bridge owns FFmpeg/libav decoder state and exposes a compact ABI for
 * LuaJIT FFI. It loads libav libraries from the fuse-local platform folder
 * only; it does not link against libav at build time.
 */

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Export the public ABI. On Windows nothing is exported from a DLL by default;
 * elsewhere symbols already default to visible, so this is a no-op. */
#ifdef _WIN32
#define TLAV_EXPORT __declspec(dllexport)
#else
#define TLAV_EXPORT
#endif

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

typedef struct TlavApi {
	void* avutil;
	void* avcodec;
	void* avformat;
	void* swscale;
	void* swresample;

	const char* (*av_version_info)(void);
	int (*av_strerror)(int errnum, char* errbuf, size_t errbuf_size);
	const char* (*av_get_pix_fmt_name)(enum AVPixelFormat pix_fmt);

	/* Runtime soname-major reporters, used to reject ABI-incompatible libs. */
	unsigned (*avutil_version)(void);
	unsigned (*avcodec_version)(void);
	unsigned (*avformat_version)(void);
	unsigned (*swscale_version)(void);

	int (*avformat_open_input)(AVFormatContext** ps, const char* url, const AVInputFormat* fmt, AVDictionary** options);
	int (*avformat_find_stream_info)(AVFormatContext* ic, AVDictionary** options);
	void (*avformat_close_input)(AVFormatContext** s);
	int (*av_find_best_stream)(AVFormatContext* ic, enum AVMediaType type, int wanted_stream_nb, int related_stream, const AVCodec** decoder_ret, int flags);
	int (*av_read_frame)(AVFormatContext* s, AVPacket* pkt);
	int (*av_seek_frame)(AVFormatContext* s, int stream_index, int64_t timestamp, int flags);

	const AVCodec* (*avcodec_find_decoder)(enum AVCodecID id);
	AVCodecContext* (*avcodec_alloc_context3)(const AVCodec* codec);
	int (*avcodec_parameters_to_context)(AVCodecContext* codec, const AVCodecParameters* par);
	int (*avcodec_open2)(AVCodecContext* avctx, const AVCodec* codec, AVDictionary** options);
	void (*avcodec_free_context)(AVCodecContext** avctx);
	int (*avcodec_send_packet)(AVCodecContext* avctx, const AVPacket* avpkt);
	int (*avcodec_receive_frame)(AVCodecContext* avctx, AVFrame* frame);
	void (*avcodec_flush_buffers)(AVCodecContext* avctx);
	const char* (*avcodec_get_name)(enum AVCodecID id);
	AVPacket* (*av_packet_alloc)(void);
	void (*av_packet_free)(AVPacket** pkt);
	void (*av_packet_unref)(AVPacket* pkt);

	AVFrame* (*av_frame_alloc)(void);
	void (*av_frame_free)(AVFrame** frame);

	struct SwsContext* (*sws_getContext)(int srcW, int srcH, enum AVPixelFormat srcFormat, int dstW, int dstH, enum AVPixelFormat dstFormat, int flags, SwsFilter* srcFilter, SwsFilter* dstFilter, const double* param);
	int (*sws_scale)(struct SwsContext* c, const uint8_t* const srcSlice[], const int srcStride[], int srcSliceY, int srcSliceH, uint8_t* const dst[], const int dstStride[]);
	void (*sws_freeContext)(struct SwsContext* swsContext);

	/* Optional hardware-decode symbols; absent on older libav builds. */
	int hwSupported;
	const AVCodecHWConfig* (*avcodec_get_hw_config)(const AVCodec* codec, int index);
	int (*av_hwdevice_ctx_create)(AVBufferRef** device_ctx, enum AVHWDeviceType type, const char* device, AVDictionary* opts, int flags);
	int (*av_hwframe_transfer_data)(AVFrame* dst, const AVFrame* src, int flags);
	AVBufferRef* (*av_buffer_ref)(AVBufferRef* buf);
	void (*av_buffer_unref)(AVBufferRef** buf);
	void (*av_frame_unref)(AVFrame* frame);
	const char* (*av_hwdevice_get_type_name)(enum AVHWDeviceType type);
} TlavApi;

typedef struct TlavDecoder {
	AVFormatContext* format;
	AVCodecContext* codec;
	const AVCodec* decoder;
	AVStream* stream;
	AVFrame* frame;
	AVPacket* packet;
	struct SwsContext* sws;
	AVBufferRef* hw_device_ctx;
	AVFrame* sw_frame;
	enum AVPixelFormat hw_pix_fmt;
	enum AVHWDeviceType hw_type;
	int using_hw;
	char hw_name[64];
	uint8_t* rgba;
	int rgba_size;
	int width;
	int height;
	int last_scaled_rows;
	int first_row_alpha_min;
	int first_row_alpha_max;
	int first_row_alpha_zero_count;
	int last_row_alpha_min;
	int last_row_alpha_max;
	int last_row_alpha_zero_count;
	unsigned int first_row_sample;
	unsigned int last_row_sample;
	enum AVPixelFormat sws_source_format;
	int stream_index;
	double fps;
	double duration;
	int64_t frame_count;
	int64_t current_frame;
	int draining;
	int64_t start_time;
	AVRational time_base;
	char codec_name[64];
	char pixel_format[64];
	char format_name[128];
} TlavDecoder;

static TlavApi gApi;
static int gApiLoaded = 0;
static char gLibraryDir[PATH_MAX] = {0};
static char gLastError[1024] = {0};
static char gLastDebug[2048] = {0};

#ifdef _WIN32
static CRITICAL_SECTION gMutex;
static int gMutexInitialized = 0;
static void lockMutex(void)
{
	if (!gMutexInitialized) {
		InitializeCriticalSection(&gMutex);
		gMutexInitialized = 1;
	}
	EnterCriticalSection(&gMutex);
}
static void unlockMutex(void)
{
	LeaveCriticalSection(&gMutex);
}
#else
static pthread_mutex_t gMutex = PTHREAD_MUTEX_INITIALIZER;
static void lockMutex(void)
{
	pthread_mutex_lock(&gMutex);
}
static void unlockMutex(void)
{
	pthread_mutex_unlock(&gMutex);
}
#endif

static void setError(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(gLastError, sizeof(gLastError), fmt, args);
	gLastError[sizeof(gLastError) - 1] = '\0';
	va_end(args);
}

static void clearError(void)
{
	gLastError[0] = '\0';
}

static void setDebug(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(gLastDebug, sizeof(gLastDebug), fmt, args);
	gLastDebug[sizeof(gLastDebug) - 1] = '\0';
	va_end(args);
}

static void copyString(char* dst, size_t dstSize, const char* src)
{
	if (dstSize == 0) {
		return;
	}

	if (!src) {
		dst[0] = '\0';
		return;
	}

	snprintf(dst, dstSize, "%s", src);
	dst[dstSize - 1] = '\0';
}

static double rationalToDouble(AVRational r)
{
	if (r.den == 0 || r.num == 0) {
		return 0.0;
	}

	return (double)r.num / (double)r.den;
}

static int64_t roundDoubleToInt64(double value)
{
	if (value < 0.0) {
		return (int64_t)(value - 0.5);
	}

	return (int64_t)(value + 0.5);
}

static void formatAvError(int err, char* dst, size_t dstSize)
{
	if (gApi.av_strerror && gApi.av_strerror(err, dst, dstSize) == 0) {
		dst[dstSize - 1] = '\0';
		return;
	}

	snprintf(dst, dstSize, "FFmpeg error %d", err);
	dst[dstSize - 1] = '\0';
}

static void joinPath(char* dst, size_t dstSize, const char* dir, const char* fileName)
{
	if (dstSize == 0) return;
	size_t dirLen = strlen(dir);
	int needSep = dirLen > 0 && dir[dirLen - 1] != '/' && dir[dirLen - 1] != '\\';
#ifdef _WIN32
	int ret = snprintf(dst, dstSize, "%s%s%s", dir, needSep ? "\\" : "", fileName);
#else
	int ret = snprintf(dst, dstSize, "%s%s%s", dir, needSep ? "/" : "", fileName);
#endif
	if (ret < 0 || (size_t)ret >= dstSize) dst[dstSize - 1] = '\0';
}

/* Build runtime file names from the library root. The exact name prefers the
 * major this bridge was built against; the plain name lets package symlinks or
 * unversioned DLLs select the installed runtime without hardcoded major lists. */
static void exactLibName(char* dst, size_t dstSize, const char* libRoot, int major)
{
#ifdef _WIN32
	snprintf(dst, dstSize, "%s-%d.dll", libRoot, major);
#elif defined(__APPLE__)
	snprintf(dst, dstSize, "lib%s.%d.dylib", libRoot, major);
#else
	snprintf(dst, dstSize, "lib%s.so.%d", libRoot, major);
#endif
	dst[dstSize - 1] = '\0';
}

static void plainLibName(char* dst, size_t dstSize, const char* libRoot)
{
#ifdef _WIN32
	snprintf(dst, dstSize, "%s.dll", libRoot);
#elif defined(__APPLE__)
	snprintf(dst, dstSize, "lib%s.dylib", libRoot);
#else
	snprintf(dst, dstSize, "lib%s.so", libRoot);
#endif
	dst[dstSize - 1] = '\0';
}

static void* loadSharedObject(const char* path)
{
#ifdef _WIN32
	return (void*)LoadLibraryA(path);
#else
	return dlopen(path, RTLD_NOW | RTLD_GLOBAL);
#endif
}

static void* loadSymbol(void* handle, const char* symbolName)
{
#ifdef _WIN32
	return (void*)GetProcAddress((HMODULE)handle, symbolName);
#else
	return dlsym(handle, symbolName);
#endif
}

/* ---- Dynamic library discovery ---- */

#ifdef _WIN32
/* Extract version from "avcodec-<major>.dll", or -1. */
static int dllVersion(const char* name)
{
	const char* dot = strrchr(name, '.');
	if (!dot) return -1;
	const char* dash = NULL;
	for (const char* p = dot - 1; p >= name; p--) {
		if (*p == '-') { dash = p; break; }
	}
	if (!dash) return -1;
	return atoi(dash + 1);
}
static int matchLibrary(const char* name, const char* lib) {
	char pat[64];
	snprintf(pat, sizeof(pat), "%s-", lib);
	return strncmp(name, pat, strlen(pat)) == 0 && strstr(name, ".dll");
}
#elif defined(__APPLE__)
/* Extract version from "libavutil.<major>.dylib", or -1. */
static int dylibVersion(const char* name)
{
	const char* dot = strrchr(name, '.');
	if (!dot) return -1;
	const char* prevDot = NULL;
	for (const char* p = dot - 1; p >= name; p--) {
		if (*p == '.') { prevDot = p; break; }
	}
	if (!prevDot) return -1;
	char buf[32];
	size_t len = (size_t)(dot - prevDot - 1);
	if (len >= sizeof(buf)) return -1;
	memcpy(buf, prevDot + 1, len);
	buf[len] = '\0';
	int v = atoi(buf);
	return v > 0 ? v : -1;
}
static int matchLibrary(const char* name, const char* lib) {
	char pat[64];
	snprintf(pat, sizeof(pat), "lib%s.", lib);
	return strncmp(name, pat, strlen(pat)) == 0 && strstr(name, ".dylib");
}
#else
/* Extract version from "libavutil.so.<major>", or -1. */
static int soVersion(const char* name)
{
	const char* dot = strrchr(name, '.');
	if (!dot) return -1;
	int v = atoi(dot + 1);
	return v > 0 ? v : -1;
}
static int matchLibrary(const char* name, const char* lib) {
	char pat[64];
	snprintf(pat, sizeof(pat), "lib%s.", lib);
	return strncmp(name, pat, strlen(pat)) == 0;
}
#endif

/* d_name can be up to NAME_MAX (typically 255 + NUL). */
struct LibEntry { char name[256]; int version; };

static int entrySortDesc(const void* a, const void* b) {
	return ((const struct LibEntry*)b)->version - ((const struct LibEntry*)a)->version;
}

/* Scan gLibraryDir for files matching library <lib>, return sorted entries. */
static int scanLibraries(const char* lib, struct LibEntry* entries, int max)
{
	int count = 0;
#ifdef _WIN32
	char pattern[PATH_MAX];
	snprintf(pattern, sizeof(pattern), "%s\\%s-*.dll", gLibraryDir, lib);
	WIN32_FIND_DATAA ffd;
	HANDLE hFind = FindFirstFileA(pattern, &ffd);
	if (hFind == INVALID_HANDLE_VALUE) return 0;
	do {
		if (count >= max) break;
		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		snprintf(entries[count].name, sizeof(entries[count].name), "%s", ffd.cFileName);
		entries[count].version = dllVersion(ffd.cFileName);
		count++;
	} while (FindNextFileA(hFind, &ffd) != 0);
	FindClose(hFind);
#else
	DIR* d = opendir(gLibraryDir);
	if (!d) return 0;
	struct dirent* entry;
	while ((entry = readdir(d)) != NULL && count < max) {
		if (!matchLibrary(entry->d_name, lib)) continue;
		snprintf(entries[count].name, sizeof(entries[count].name), "%s", entry->d_name);
		entries[count].version =
#ifdef __APPLE__
			dylibVersion(entry->d_name);
#else
			soVersion(entry->d_name);
#endif
		count++;
	}
	closedir(d);
#endif
	if (count > 1) qsort(entries, (size_t)count, sizeof(struct LibEntry), entrySortDesc);
	return count;
}

/* Try to load a library, preferring the exact major this bridge was built
 * against, then the highest discovered file, then the unversioned name. */
static void* loadLibrary(const char* label, const char* libRoot, int preferredMajor)
{
	char path[PATH_MAX];

	if (preferredMajor > 0) {
		char exact[64];
		exactLibName(exact, sizeof(exact), libRoot, preferredMajor);
		joinPath(path, sizeof(path), gLibraryDir, exact);
		void* handle = loadSharedObject(path);
		if (handle) return handle;
	}

	struct LibEntry entries[32];
	int n = scanLibraries(libRoot, entries, 32);
	if (n > 0) {
		for (int i = 0; i < n; i++) {
			joinPath(path, sizeof(path), gLibraryDir, entries[i].name);
			void* handle = loadSharedObject(path);
			if (handle) return handle;
		}
	}
	char plain[64];
	plainLibName(plain, sizeof(plain), libRoot);
	joinPath(path, sizeof(path), gLibraryDir, plain);
	void* handle = loadSharedObject(path);
	if (handle) return handle;

	setError("Missing %s in %s", label, gLibraryDir);
	return NULL;
}

static int loadRequiredSymbol(void* handle, const char* symbolName, void** dst)
{
	*dst = loadSymbol(handle, symbolName);
	if (!*dst) {
		setError("Missing symbol %s", symbolName);
		return 0;
	}

	return 1;
}

#define LOAD_SYMBOL(handle, name) \
	do { \
		if (!loadRequiredSymbol((handle), #name, (void**)&gApi.name)) { \
			return 0; \
		} \
	} while (0)

static void loadOptionalSymbol(void* handle, const char* symbolName, void** dst)
{
	*dst = loadSymbol(handle, symbolName);
}

#define LOAD_OPTIONAL(handle, name) \
	loadOptionalSymbol((handle), #name, (void**)&gApi.name)

/* libav packs the soname major into the high 8 bits of the version word. */
static int avMajor(unsigned version)
{
	return (int)((version >> 16) & 0xff);
}

/* Map a libavcodec soname major to its FFmpeg release for human-readable errors. */
static int ffmpegReleaseFromAvcodecMajor(int avcodecMajor)
{
	switch (avcodecMajor) {
		case 62: return 8;
		case 61: return 7;
		case 60: return 6;
		case 59: return 5;
		case 58: return 4;
		default: return 0;
	}
}

/*
 * The bridge reads libav structs (AVCodecContext, AVFrame, ...) by field, so it
 * is only ABI-compatible with the FFmpeg major it was compiled against. The
 * loader will happily open a different major, which then reads fields at the
 * wrong offsets (garbage sizes, failed decodes). Reject any major mismatch here
 * with an actionable message instead of corrupting output downstream.
 */
static int checkLibavVersions(void)
{
	struct VersionCheck {
		const char* name;
		unsigned (*fn)(void);
		int expected;
	} checks[4] = {
		{"libavutil", gApi.avutil_version, LIBAVUTIL_VERSION_MAJOR},
		{"libavcodec", gApi.avcodec_version, LIBAVCODEC_VERSION_MAJOR},
		{"libavformat", gApi.avformat_version, LIBAVFORMAT_VERSION_MAJOR},
		{"libswscale", gApi.swscale_version, LIBSWSCALE_VERSION_MAJOR},
	};

	char detail[512];
	size_t off = 0;
	int bad = 0;
	for (int i = 0; i < 4; i++) {
		int got = checks[i].fn ? avMajor(checks[i].fn()) : -1;
		if (got == checks[i].expected) {
			continue;
		}
		bad++;
		int written = snprintf(detail + off, sizeof(detail) - off, "%s%s major %d (need %d)",
			off ? ", " : "", checks[i].name, got, checks[i].expected);
		if (written < 0) break;
		off += (size_t)written;
		if (off >= sizeof(detail)) { off = sizeof(detail) - 1; break; }
	}

	if (bad) {
		int release = ffmpegReleaseFromAvcodecMajor(LIBAVCODEC_VERSION_MAJOR);
		if (release > 0) {
			setError("FFmpeg ABI mismatch: %s. This bridge needs FFmpeg %d.x runtime libraries; copy matching libav*/libsw* files into %s.",
				detail, release, gLibraryDir);
		} else {
			setError("FFmpeg ABI mismatch: %s. Copy runtime libraries matching this bridge build into %s.",
				detail, gLibraryDir);
		}
		setDebug("version-mismatch %s", detail);
		return 0;
	}

	setDebug("version-ok avutil=%d avcodec=%d avformat=%d swscale=%d",
		avMajor(gApi.avutil_version()), avMajor(gApi.avcodec_version()),
		avMajor(gApi.avformat_version()), avMajor(gApi.swscale_version()));
	return 1;
}

static int loadApi(void)
{
	if (gApiLoaded) {
		return 1;
	}

	if (gLibraryDir[0] == '\0') {
		setError("LibAV library directory was not configured");
		return 0;
	}

	memset(&gApi, 0, sizeof(gApi));
	gApi.avutil = loadLibrary("libavutil", "avutil", LIBAVUTIL_VERSION_MAJOR);
	if (!gApi.avutil) return 0;
	gApi.swresample = loadLibrary("libswresample", "swresample", 0);
	if (!gApi.swresample) return 0;
	gApi.avcodec = loadLibrary("libavcodec", "avcodec", LIBAVCODEC_VERSION_MAJOR);
	if (!gApi.avcodec) return 0;
	gApi.avformat = loadLibrary("libavformat", "avformat", LIBAVFORMAT_VERSION_MAJOR);
	if (!gApi.avformat) return 0;
	gApi.swscale = loadLibrary("libswscale", "swscale", LIBSWSCALE_VERSION_MAJOR);
	if (!gApi.swscale) return 0;

	/* Reject ABI-incompatible majors before any struct field is read. */
	LOAD_SYMBOL(gApi.avutil, avutil_version);
	LOAD_SYMBOL(gApi.avcodec, avcodec_version);
	LOAD_SYMBOL(gApi.avformat, avformat_version);
	LOAD_SYMBOL(gApi.swscale, swscale_version);
	if (!checkLibavVersions()) {
		return 0;
	}

	LOAD_SYMBOL(gApi.avutil, av_version_info);
	LOAD_SYMBOL(gApi.avutil, av_strerror);
	LOAD_SYMBOL(gApi.avutil, av_get_pix_fmt_name);

	LOAD_SYMBOL(gApi.avformat, avformat_open_input);
	LOAD_SYMBOL(gApi.avformat, avformat_find_stream_info);
	LOAD_SYMBOL(gApi.avformat, avformat_close_input);
	LOAD_SYMBOL(gApi.avformat, av_find_best_stream);
	LOAD_SYMBOL(gApi.avformat, av_read_frame);
	LOAD_SYMBOL(gApi.avformat, av_seek_frame);

	LOAD_SYMBOL(gApi.avcodec, avcodec_find_decoder);
	LOAD_SYMBOL(gApi.avcodec, avcodec_alloc_context3);
	LOAD_SYMBOL(gApi.avcodec, avcodec_parameters_to_context);
	LOAD_SYMBOL(gApi.avcodec, avcodec_open2);
	LOAD_SYMBOL(gApi.avcodec, avcodec_free_context);
	LOAD_SYMBOL(gApi.avcodec, avcodec_send_packet);
	LOAD_SYMBOL(gApi.avcodec, avcodec_receive_frame);
	LOAD_SYMBOL(gApi.avcodec, avcodec_flush_buffers);
	LOAD_SYMBOL(gApi.avcodec, avcodec_get_name);
	LOAD_SYMBOL(gApi.avcodec, av_packet_alloc);
	LOAD_SYMBOL(gApi.avcodec, av_packet_free);
	LOAD_SYMBOL(gApi.avcodec, av_packet_unref);

	LOAD_SYMBOL(gApi.avutil, av_frame_alloc);
	LOAD_SYMBOL(gApi.avutil, av_frame_free);

	LOAD_SYMBOL(gApi.swscale, sws_getContext);
	LOAD_SYMBOL(gApi.swscale, sws_scale);
	LOAD_SYMBOL(gApi.swscale, sws_freeContext);

	LOAD_OPTIONAL(gApi.avcodec, avcodec_get_hw_config);
	LOAD_OPTIONAL(gApi.avutil, av_hwdevice_ctx_create);
	LOAD_OPTIONAL(gApi.avutil, av_hwframe_transfer_data);
	LOAD_OPTIONAL(gApi.avutil, av_buffer_ref);
	LOAD_OPTIONAL(gApi.avutil, av_buffer_unref);
	LOAD_OPTIONAL(gApi.avutil, av_frame_unref);
	LOAD_OPTIONAL(gApi.avutil, av_hwdevice_get_type_name);
	gApi.hwSupported = gApi.avcodec_get_hw_config && gApi.av_hwdevice_ctx_create
		&& gApi.av_hwframe_transfer_data && gApi.av_buffer_ref
		&& gApi.av_buffer_unref && gApi.av_frame_unref;

	gApiLoaded = 1;
	clearError();
	return 1;
}

/*
 * get_format callback: pick our hardware pixel format if the decoder offers it.
 * Called by the decoder while configuring; dec is reached through ctx->opaque.
 */
static enum AVPixelFormat selectHwFormat(AVCodecContext* ctx, const enum AVPixelFormat* formats)
{
	TlavDecoder* dec = ctx ? (TlavDecoder*)ctx->opaque : NULL;
	if (dec && dec->using_hw) {
		for (const enum AVPixelFormat* p = formats; *p != AV_PIX_FMT_NONE; p++) {
			if (*p == dec->hw_pix_fmt) {
				return *p;
			}
		}
		setDebug("hw format %d not offered by decoder; using software", (int)dec->hw_pix_fmt);
	}

	/* Fall back to the decoder's first (software) choice. */
	return formats[0];
}

/*
 * Try to attach a hardware device the decoder can use. Walks the decoder's
 * advertised hw configs and keeps the first device that actually creates,
 * which makes the selection codec- and platform-agnostic (VAAPI, CUDA,
 * VideoToolbox, D3D11VA, ...). Leaves the decoder in software mode on failure.
 */
static void setupHardware(TlavDecoder* dec)
{
	if (!gApi.hwSupported || !dec || !dec->decoder || !dec->codec) {
		return;
	}

	for (int i = 0;; i++) {
		const AVCodecHWConfig* config = gApi.avcodec_get_hw_config(dec->decoder, i);
		if (!config) {
			setDebug("hw none usable for codec %s", dec->decoder->name ? dec->decoder->name : "?");
			return;
		}
		if (!(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
			continue;
		}

		AVBufferRef* hwctx = NULL;
		int ret = gApi.av_hwdevice_ctx_create(&hwctx, config->device_type, NULL, NULL, 0);
		if (ret < 0 || !hwctx) {
			continue;
		}

		AVFrame* swFrame = gApi.av_frame_alloc();
		if (!swFrame) {
			gApi.av_buffer_unref(&hwctx);
			return;
		}

		dec->hw_device_ctx = hwctx;
		dec->sw_frame = swFrame;
		dec->hw_pix_fmt = config->pix_fmt;
		dec->hw_type = config->device_type;
		dec->codec->hw_device_ctx = gApi.av_buffer_ref(hwctx);
		dec->codec->opaque = dec;
		dec->codec->get_format = selectHwFormat;
		dec->using_hw = 1;
		copyString(dec->hw_name, sizeof(dec->hw_name),
			gApi.av_hwdevice_get_type_name ? gApi.av_hwdevice_get_type_name(config->device_type) : "hardware");
		setDebug("hw enabled type=%s pix=%d codec=%s", dec->hw_name, (int)dec->hw_pix_fmt, dec->decoder->name ? dec->decoder->name : "?");
		return;
	}
}

static void closeDecoder(TlavDecoder* dec)
{
	if (!dec) {
		return;
	}

	if (dec->sws && gApi.sws_freeContext) {
		gApi.sws_freeContext(dec->sws);
	}
	if (dec->rgba) {
		free(dec->rgba);
	}
	if (dec->sw_frame && gApi.av_frame_free) {
		gApi.av_frame_free(&dec->sw_frame);
	}
	if (dec->frame && gApi.av_frame_free) {
		gApi.av_frame_free(&dec->frame);
	}
	if (dec->packet && gApi.av_packet_free) {
		gApi.av_packet_free(&dec->packet);
	}
	if (dec->codec && gApi.avcodec_free_context) {
		gApi.avcodec_free_context(&dec->codec);
	}
	if (dec->hw_device_ctx && gApi.av_buffer_unref) {
		gApi.av_buffer_unref(&dec->hw_device_ctx);
	}
	if (dec->format && gApi.avformat_close_input) {
		gApi.avformat_close_input(&dec->format);
	}

	free(dec);
}

static int fillInfo(TlavDecoder* dec, TlavInfo* info)
{
	if (!dec || !info) {
		return 0;
	}

	info->width = dec->width;
	info->height = dec->height;
	info->fps = dec->fps;
	info->frame_count = dec->frame_count;
	info->duration = dec->duration;
	copyString(info->codec_name, sizeof(info->codec_name), dec->codec_name);
	copyString(info->pixel_format, sizeof(info->pixel_format), dec->pixel_format);
	copyString(info->format_name, sizeof(info->format_name), dec->format_name);
	return 1;
}

static int seekDecoder(TlavDecoder* dec, int64_t targetFrame)
{
	if (!dec) {
		setError("Decoder handle is null");
		return 0;
	}

	if (targetFrame < 0) {
		targetFrame = 0;
	}

	double tb = rationalToDouble(dec->time_base);
	int64_t ts = 0;
	if (tb > 0.0 && dec->fps > 0.0) {
		double seconds = (double)targetFrame / dec->fps;
		ts = roundDoubleToInt64(seconds / tb);
		if (dec->start_time != AV_NOPTS_VALUE) {
			ts += dec->start_time;
		}
	}

	int ret = gApi.av_seek_frame(dec->format, dec->stream_index, ts, AVSEEK_FLAG_BACKWARD);
	if (ret < 0) {
		char err[256];
		formatAvError(ret, err, sizeof(err));
		setError("Seek failed for frame %lld: %s", (long long)targetFrame, err);
		return 0;
	}

	gApi.avcodec_flush_buffers(dec->codec);
	dec->current_frame = targetFrame > 0 ? targetFrame - 1 : -1;
	dec->draining = 0;
	return 1;
}

static int receiveNextFrame(TlavDecoder* dec)
{
	while (1) {
		int ret = gApi.avcodec_receive_frame(dec->codec, dec->frame);
		if (ret == 0) {
			return 1;
		}
		if (ret == AVERROR_EOF) {
			return 0;
		}
		if (ret != AVERROR(EAGAIN)) {
			char err[256];
			formatAvError(ret, err, sizeof(err));
			setError("Decode failed: %s", err);
			return 0;
		}
		if (dec->draining) {
			return 0;
		}

		while (1) {
			ret = gApi.av_read_frame(dec->format, dec->packet);
			if (ret < 0) {
				gApi.avcodec_send_packet(dec->codec, NULL);
				dec->draining = 1;
				break;
			}

			if (dec->packet->stream_index != dec->stream_index) {
				gApi.av_packet_unref(dec->packet);
				continue;
			}

			ret = gApi.avcodec_send_packet(dec->codec, dec->packet);
			gApi.av_packet_unref(dec->packet);
			if (ret < 0 && ret != AVERROR(EAGAIN)) {
				char err[256];
				formatAvError(ret, err, sizeof(err));
				setError("Could not send packet to decoder: %s", err);
				return 0;
			}
			break;
		}
	}
}

static int estimateFrameIndex(TlavDecoder* dec, int64_t* outIndex)
{
	int64_t ts = dec->frame->best_effort_timestamp;
	if (ts == AV_NOPTS_VALUE) {
		ts = dec->frame->pts;
	}

	if (ts != AV_NOPTS_VALUE && dec->fps > 0.0) {
		int64_t start = dec->start_time == AV_NOPTS_VALUE ? 0 : dec->start_time;
		double seconds = (double)(ts - start) * rationalToDouble(dec->time_base);
		int64_t idx = roundDoubleToInt64(seconds * dec->fps);
		if (idx >= 0) {
			*outIndex = idx;
			return 1;
		}
	}

	*outIndex = dec->current_frame + 1;
	return 0;
}

static int convertFrameToBuffer(TlavDecoder* dec, uint8_t* dstBuffer, int dstLineStride)
{
	AVFrame* srcFrame = dec->frame;

	/* Hardware-decoded frames live in GPU memory; pull them down to a CPU frame
	 * (typically NV12) before swscale converts to RGBA. */
	if (dec->using_hw && dec->sw_frame && dec->frame->format == dec->hw_pix_fmt) {
		gApi.av_frame_unref(dec->sw_frame);
		int xfer = gApi.av_hwframe_transfer_data(dec->sw_frame, dec->frame, 0);
		if (xfer < 0) {
			char err[256];
			formatAvError(xfer, err, sizeof(err));
			setError("Hardware frame transfer failed: %s", err);
			return 0;
		}
		srcFrame = dec->sw_frame;
	}

	int width = srcFrame->width > 0 ? srcFrame->width : dec->width;
	int height = srcFrame->height > 0 ? srcFrame->height : dec->height;
	enum AVPixelFormat sourceFormat = (enum AVPixelFormat)srcFrame->format;

	if (width <= 0 || height <= 0) {
		setError("Decoded frame has invalid size %dx%d", width, height);
		return 0;
	}

	if (!dstBuffer) {
		int needed = width * height * 4;
		if (!dec->rgba || dec->rgba_size < needed) {
			uint8_t* next = (uint8_t*)realloc(dec->rgba, (size_t)needed);
			if (!next) {
				setError("Could not allocate %d bytes for RGBA frame", needed);
				return 0;
			}
			dec->rgba = next;
			dec->rgba_size = needed;
		}

		dstBuffer = dec->rgba;
		dstLineStride = width * 4;
	} else if (dstLineStride == 0) {
		setError("Destination stride is zero");
		return 0;
	}

	if (!dec->sws || dec->width != width || dec->height != height || dec->sws_source_format != sourceFormat) {
		if (dec->sws) {
			gApi.sws_freeContext(dec->sws);
			dec->sws = NULL;
		}

		dec->sws = gApi.sws_getContext(width, height, sourceFormat, width, height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
		if (!dec->sws) {
			setError("Could not create swscale context");
			return 0;
		}

		dec->width = width;
		dec->height = height;
		dec->sws_source_format = sourceFormat;
		const char* pixName = gApi.av_get_pix_fmt_name(sourceFormat);
		copyString(dec->pixel_format, sizeof(dec->pixel_format), pixName ? pixName : "unknown");
	}

	uint8_t* dstData[4] = {dstBuffer, NULL, NULL, NULL};
	int dstStride[4] = {dstLineStride, 0, 0, 0};
	int scaled = gApi.sws_scale(dec->sws, (const uint8_t* const*)srcFrame->data, srcFrame->linesize, 0, height, dstData, dstStride);
	dec->last_scaled_rows = scaled;
	if (scaled <= 0) {
		setError("swscale produced no output");
		return 0;
	}
	if (scaled < height) {
		setError("swscale produced only %d of %d rows", scaled, height);
		return 0;
	}

	const uint8_t* firstRow = dstBuffer;
	const uint8_t* lastRow = dstBuffer + ((height - 1) * dstStride[0]);
	dec->first_row_alpha_min = 255;
	dec->first_row_alpha_max = 0;
	dec->first_row_alpha_zero_count = 0;
	dec->last_row_alpha_min = 255;
	dec->last_row_alpha_max = 0;
	dec->last_row_alpha_zero_count = 0;
	dec->first_row_sample = 0;
	dec->last_row_sample = 0;

	for (int x = 0; x < width; x++) {
		int firstOffset = x * 4;
		int lastOffset = x * 4;
		int firstAlpha = firstRow[firstOffset + 3];
		int lastAlpha = lastRow[lastOffset + 3];
		if (firstAlpha < dec->first_row_alpha_min) dec->first_row_alpha_min = firstAlpha;
		if (firstAlpha > dec->first_row_alpha_max) dec->first_row_alpha_max = firstAlpha;
		if (firstAlpha == 0) dec->first_row_alpha_zero_count++;
		if (lastAlpha < dec->last_row_alpha_min) dec->last_row_alpha_min = lastAlpha;
		if (lastAlpha > dec->last_row_alpha_max) dec->last_row_alpha_max = lastAlpha;
		if (lastAlpha == 0) dec->last_row_alpha_zero_count++;

		dec->first_row_sample = (dec->first_row_sample + ((unsigned int)firstRow[firstOffset] * 3u) + ((unsigned int)firstRow[firstOffset + 1] * 5u) + ((unsigned int)firstRow[firstOffset + 2] * 7u) + ((unsigned int)firstAlpha * 11u)) % 1000000007u;
		dec->last_row_sample = (dec->last_row_sample + ((unsigned int)lastRow[lastOffset] * 13u) + ((unsigned int)lastRow[lastOffset + 1] * 17u) + ((unsigned int)lastRow[lastOffset + 2] * 19u) + ((unsigned int)lastAlpha * 23u)) % 1000000007u;
	}

	return 1;
}

static int convertFrame(TlavDecoder* dec)
{
	return convertFrameToBuffer(dec, NULL, 0);
}

static unsigned int sampleFrameBytes(TlavDecoder* dec)
{
	if (!dec || !dec->rgba || dec->rgba_size <= 0) {
		return 0;
	}

	int limit = dec->rgba_size < 4096 ? dec->rgba_size : 4096;
	unsigned int sample = 0;
	for (int i = 0; i < limit; i++) {
		sample = (sample + (unsigned int)dec->rgba[i] * (unsigned int)((i % 31) + 1)) % 1000000007u;
	}

	return sample;
}

static void invalidateRgbaCache(TlavDecoder* dec)
{
	if (!dec || !dec->rgba) {
		return;
	}

	free(dec->rgba);
	dec->rgba = NULL;
	dec->rgba_size = 0;
}

static int copyRgbaCacheToBuffer(TlavDecoder* dec, uint8_t* dstBuffer, int dstLineStride)
{
	if (!dec || !dec->rgba || !dstBuffer || dec->width <= 0 || dec->height <= 0) {
		setError("No cached RGBA frame available");
		return 0;
	}

	int rowBytes = dec->width * 4;
	for (int y = 0; y < dec->height; y++) {
		memcpy(dstBuffer + ((ptrdiff_t)y * dstLineStride), dec->rgba + ((ptrdiff_t)y * rowBytes), (size_t)rowBytes);
	}

	return 1;
}

TLAV_EXPORT void tlav_set_library_dir(const char* dir)
{
	lockMutex();
	copyString(gLibraryDir, sizeof(gLibraryDir), dir);
	unlockMutex();
}

TLAV_EXPORT const char* tlav_version(void)
{
	lockMutex();
	if (!loadApi()) {
		unlockMutex();
		return "bridge loaded, libav unavailable";
	}

	static char version[128];
	snprintf(version, sizeof(version), "bridge 1.2.0, libav %s", gApi.av_version_info ? gApi.av_version_info() : "unknown");
	version[sizeof(version) - 1] = '\0';
	unlockMutex();
	return version;
}

TLAV_EXPORT const char* tlav_last_error(void)
{
	return gLastError;
}

TLAV_EXPORT const char* tlav_last_debug(void)
{
	return gLastDebug;
}

/* Name of the active hardware decode backend (e.g. "cuda", "vaapi"), or "" when
 * decoding in software. Returns a pointer into stable decoder state. */
TLAV_EXPORT const char* tlav_hw_accel(void* handle)
{
	TlavDecoder* dec = (TlavDecoder*)handle;
	return (dec && dec->using_hw) ? dec->hw_name : "";
}

TLAV_EXPORT void* tlav_open(const char* path, TlavInfo* info)
{
	lockMutex();
	clearError();

	if (!path || path[0] == '\0') {
		setError("Input path is empty");
		setDebug("open failed empty-path");
		unlockMutex();
		return NULL;
	}

	if (!loadApi()) {
		setDebug("open failed load-api path=%s error=%s", path, gLastError);
		unlockMutex();
		return NULL;
	}

	TlavDecoder* dec = (TlavDecoder*)calloc(1, sizeof(TlavDecoder));
	if (!dec) {
		setError("Could not allocate decoder state");
		setDebug("open failed allocate-decoder path=%s", path);
		unlockMutex();
		return NULL;
	}

	dec->stream_index = -1;
	dec->current_frame = -1;
	dec->start_time = AV_NOPTS_VALUE;
	dec->sws_source_format = AV_PIX_FMT_NONE;
	dec->hw_pix_fmt = AV_PIX_FMT_NONE;

	int ret = gApi.avformat_open_input(&dec->format, path, NULL, NULL);
	if (ret < 0) {
		char err[256];
		formatAvError(ret, err, sizeof(err));
		setError("Could not open input: %s", err);
		setDebug("open failed avformat-open path=%s error=%s", path, err);
		closeDecoder(dec);
		unlockMutex();
		return NULL;
	}

	ret = gApi.avformat_find_stream_info(dec->format, NULL);
	if (ret < 0) {
		char err[256];
		formatAvError(ret, err, sizeof(err));
		setError("Could not read stream info: %s", err);
		setDebug("open failed stream-info path=%s error=%s", path, err);
		closeDecoder(dec);
		unlockMutex();
		return NULL;
	}

	dec->stream_index = gApi.av_find_best_stream(dec->format, AVMEDIA_TYPE_VIDEO, -1, -1, &dec->decoder, 0);
	if (dec->stream_index < 0) {
		setError("No video stream found");
		setDebug("open failed no-video-stream path=%s", path);
		closeDecoder(dec);
		unlockMutex();
		return NULL;
	}

	dec->stream = dec->format->streams[dec->stream_index];
	if (!dec->decoder) {
		dec->decoder = gApi.avcodec_find_decoder(dec->stream->codecpar->codec_id);
	}
	if (!dec->decoder) {
		setError("No decoder found for video stream");
		setDebug("open failed no-decoder path=%s stream=%d", path, dec->stream_index);
		closeDecoder(dec);
		unlockMutex();
		return NULL;
	}

	dec->codec = gApi.avcodec_alloc_context3(dec->decoder);
	if (!dec->codec) {
		setError("Could not allocate codec context");
		setDebug("open failed allocate-codec path=%s stream=%d", path, dec->stream_index);
		closeDecoder(dec);
		unlockMutex();
		return NULL;
	}

	ret = gApi.avcodec_parameters_to_context(dec->codec, dec->stream->codecpar);
	if (ret < 0) {
		char err[256];
		formatAvError(ret, err, sizeof(err));
		setError("Could not copy codec parameters: %s", err);
		setDebug("open failed codec-parameters path=%s stream=%d error=%s", path, dec->stream_index, err);
		closeDecoder(dec);
		unlockMutex();
		return NULL;
	}

	/* Attach a hardware device if one is available; harmless no-op otherwise.
	 * Must happen before avcodec_open2 so get_format/hw_device_ctx take effect. */
	setupHardware(dec);

	ret = gApi.avcodec_open2(dec->codec, dec->decoder, NULL);
	if (ret < 0) {
		char err[256];
		formatAvError(ret, err, sizeof(err));
		setError("Could not open decoder: %s", err);
		setDebug("open failed codec-open path=%s stream=%d codec=%s error=%s", path, dec->stream_index, dec->decoder->name ? dec->decoder->name : "unknown", err);
		closeDecoder(dec);
		unlockMutex();
		return NULL;
	}

	dec->packet = gApi.av_packet_alloc();
	dec->frame = gApi.av_frame_alloc();
	if (!dec->packet || !dec->frame) {
		setError("Could not allocate decode packet/frame");
		setDebug("open failed allocate-packet-frame path=%s stream=%d", path, dec->stream_index);
		closeDecoder(dec);
		unlockMutex();
		return NULL;
	}

	dec->width = dec->codec->width > 0 ? dec->codec->width : dec->stream->codecpar->width;
	dec->height = dec->codec->height > 0 ? dec->codec->height : dec->stream->codecpar->height;
	dec->time_base = dec->stream->time_base;
	dec->start_time = dec->stream->start_time;
	dec->fps = rationalToDouble(dec->stream->avg_frame_rate);
	if (dec->fps <= 0.0) {
		dec->fps = rationalToDouble(dec->stream->r_frame_rate);
	}
	if (dec->fps <= 0.0) {
		dec->fps = 24.0;
	}

	if (dec->stream->duration != AV_NOPTS_VALUE && rationalToDouble(dec->time_base) > 0.0) {
		dec->duration = (double)dec->stream->duration * rationalToDouble(dec->time_base);
	} else if (dec->format->duration != AV_NOPTS_VALUE) {
		dec->duration = (double)dec->format->duration / (double)AV_TIME_BASE;
	}

	if (dec->stream->nb_frames > 0) {
		dec->frame_count = dec->stream->nb_frames;
	} else if (dec->duration > 0.0 && dec->fps > 0.0) {
		dec->frame_count = roundDoubleToInt64(dec->duration * dec->fps);
	}

	copyString(dec->codec_name, sizeof(dec->codec_name), gApi.avcodec_get_name(dec->stream->codecpar->codec_id));
	const char* pixName = gApi.av_get_pix_fmt_name(dec->codec->pix_fmt);
	copyString(dec->pixel_format, sizeof(dec->pixel_format), pixName ? pixName : "unknown");
	copyString(dec->format_name, sizeof(dec->format_name), dec->format->iformat && dec->format->iformat->name ? dec->format->iformat->name : "unknown");

	fillInfo(dec, info);
	setDebug("open path=%s stream=%d size=%dx%d fps=%.9g frames=%lld duration=%.9g codec=%s pix=%s format=%s hw=%s",
		path,
		dec->stream_index,
		dec->width,
		dec->height,
		dec->fps,
		(long long)dec->frame_count,
		dec->duration,
		dec->codec_name,
		dec->pixel_format,
		dec->format_name,
		dec->using_hw ? dec->hw_name : "off");
	unlockMutex();
	return dec;
}

TLAV_EXPORT int tlav_decode(void* handle, int64_t frameIndex, TlavFrame* outFrame)
{
	lockMutex();
	clearError();

	TlavDecoder* dec = (TlavDecoder*)handle;
	int64_t requestedFrame = frameIndex;
	int64_t currentBefore = dec ? dec->current_frame : -999999;
	if (!dec || !outFrame) {
		setError("Decode called with invalid handle");
		setDebug("decode failed invalid-handle requested=%lld", (long long)requestedFrame);
		unlockMutex();
		return 0;
	}

	if (frameIndex < 0) {
		frameIndex = 0;
	}
	if (dec->frame_count > 0 && frameIndex >= dec->frame_count) {
		frameIndex = dec->frame_count - 1;
	}

	if (dec->rgba && frameIndex == dec->current_frame) {
		outFrame->data = dec->rgba;
		outFrame->width = dec->width;
		outFrame->height = dec->height;
		outFrame->stride = dec->width * 4;
		outFrame->frame_index = dec->current_frame;
		setDebug("decode cache-hit requested=%lld clamped=%lld currentBefore=%lld returned=%lld size=%dx%d stride=%d ptr=%p sample=%u",
			(long long)requestedFrame,
			(long long)frameIndex,
			(long long)currentBefore,
			(long long)outFrame->frame_index,
			outFrame->width,
			outFrame->height,
			outFrame->stride,
			(void*)outFrame->data,
			sampleFrameBytes(dec));
		unlockMutex();
		return 1;
	}

	int didSeek = 0;
	if (frameIndex < dec->current_frame || (!dec->rgba && frameIndex == dec->current_frame) || frameIndex - dec->current_frame > 90 || (dec->current_frame < 0 && frameIndex > 30)) {
		if (!seekDecoder(dec, frameIndex)) {
			setDebug("decode seek-failed requested=%lld clamped=%lld currentBefore=%lld error=%s",
				(long long)requestedFrame,
				(long long)frameIndex,
				(long long)currentBefore,
				gLastError);
			unlockMutex();
			return 0;
		}
		didSeek = 1;
	}

	int skippedFrames = 0;
	while (1) {
		if (!receiveNextFrame(dec)) {
			if (dec->rgba) {
				outFrame->data = dec->rgba;
				outFrame->width = dec->width;
				outFrame->height = dec->height;
				outFrame->stride = dec->width * 4;
				outFrame->frame_index = dec->current_frame;
				setDebug("decode eof-return-cache requested=%lld clamped=%lld currentBefore=%lld returned=%lld seek=%d skipped=%d size=%dx%d stride=%d ptr=%p sample=%u",
					(long long)requestedFrame,
					(long long)frameIndex,
					(long long)currentBefore,
					(long long)outFrame->frame_index,
					didSeek,
					skippedFrames,
					outFrame->width,
					outFrame->height,
					outFrame->stride,
					(void*)outFrame->data,
					sampleFrameBytes(dec));
				unlockMutex();
				return 1;
			}

			if (gLastError[0] == '\0') {
				setError("No decoded frame available");
			}
			setDebug("decode failed-no-frame requested=%lld clamped=%lld currentBefore=%lld seek=%d skipped=%d error=%s",
				(long long)requestedFrame,
				(long long)frameIndex,
				(long long)currentBefore,
				didSeek,
				skippedFrames,
				gLastError);
			unlockMutex();
			return 0;
		}

		int64_t decodedIndex = 0;
		int hasTimestampIndex = estimateFrameIndex(dec, &decodedIndex);
		if (hasTimestampIndex && decodedIndex < frameIndex) {
			dec->current_frame = decodedIndex;
			skippedFrames++;
			continue;
		}
		if (!hasTimestampIndex && decodedIndex < frameIndex) {
			dec->current_frame = decodedIndex;
			skippedFrames++;
			continue;
		}

		if (!convertFrame(dec)) {
			setDebug("decode convert-failed requested=%lld clamped=%lld currentBefore=%lld decoded=%lld hasTimestamp=%d seek=%d skipped=%d error=%s",
				(long long)requestedFrame,
				(long long)frameIndex,
				(long long)currentBefore,
				(long long)decodedIndex,
				hasTimestampIndex,
				didSeek,
				skippedFrames,
				gLastError);
			unlockMutex();
			return 0;
		}

		dec->current_frame = frameIndex;
		outFrame->data = dec->rgba;
		outFrame->width = dec->width;
		outFrame->height = dec->height;
		outFrame->stride = dec->width * 4;
		outFrame->frame_index = frameIndex;
		setDebug("decode converted requested=%lld clamped=%lld currentBefore=%lld decoded=%lld hasTimestamp=%d seek=%d skipped=%d returned=%lld size=%dx%d stride=%d swsRows=%d firstA=%d-%d firstA0=%d lastA=%d-%d lastA0=%d firstRowSample=%u lastRowSample=%u ptr=%p sample=%u",
			(long long)requestedFrame,
			(long long)frameIndex,
			(long long)currentBefore,
			(long long)decodedIndex,
			hasTimestampIndex,
			didSeek,
			skippedFrames,
			(long long)outFrame->frame_index,
			outFrame->width,
			outFrame->height,
			outFrame->stride,
			dec->last_scaled_rows,
			dec->first_row_alpha_min,
			dec->first_row_alpha_max,
			dec->first_row_alpha_zero_count,
			dec->last_row_alpha_min,
			dec->last_row_alpha_max,
			dec->last_row_alpha_zero_count,
			dec->first_row_sample,
			dec->last_row_sample,
			(void*)outFrame->data,
			sampleFrameBytes(dec));
		unlockMutex();
		return 1;
	}
}

TLAV_EXPORT int tlav_decode_into(void* handle, int64_t frameIndex, uint8_t* dstBuffer, int dstLineStride, TlavFrame* outFrame)
{
	lockMutex();
	clearError();

	TlavDecoder* dec = (TlavDecoder*)handle;
	int64_t requestedFrame = frameIndex;
	int64_t currentBefore = dec ? dec->current_frame : -999999;
	if (!dec || !outFrame || !dstBuffer || dstLineStride == 0) {
		setError("Direct decode called with invalid handle or destination");
		setDebug("decode-into failed invalid-args requested=%lld dst=%p stride=%d", (long long)requestedFrame, (void*)dstBuffer, dstLineStride);
		unlockMutex();
		return 0;
	}

	if (frameIndex < 0) {
		frameIndex = 0;
	}
	if (dec->frame_count > 0 && frameIndex >= dec->frame_count) {
		frameIndex = dec->frame_count - 1;
	}

	if (dec->rgba && frameIndex == dec->current_frame) {
		if (!copyRgbaCacheToBuffer(dec, dstBuffer, dstLineStride)) {
			setDebug("decode-into cache-copy-failed requested=%lld clamped=%lld error=%s", (long long)requestedFrame, (long long)frameIndex, gLastError);
			unlockMutex();
			return 0;
		}

		outFrame->data = dstBuffer;
		outFrame->width = dec->width;
		outFrame->height = dec->height;
		outFrame->stride = dstLineStride;
		outFrame->frame_index = dec->current_frame;
		setDebug("decode-into cache-hit requested=%lld clamped=%lld currentBefore=%lld returned=%lld size=%dx%d stride=%d dst=%p sample=%u",
			(long long)requestedFrame,
			(long long)frameIndex,
			(long long)currentBefore,
			(long long)outFrame->frame_index,
			outFrame->width,
			outFrame->height,
			outFrame->stride,
			(void*)dstBuffer,
			sampleFrameBytes(dec));
		unlockMutex();
		return 1;
	}

	int didSeek = 0;
	if (frameIndex < dec->current_frame || (!dec->rgba && frameIndex == dec->current_frame) || frameIndex - dec->current_frame > 90 || (dec->current_frame < 0 && frameIndex > 30)) {
		if (!seekDecoder(dec, frameIndex)) {
			setDebug("decode-into seek-failed requested=%lld clamped=%lld currentBefore=%lld error=%s",
				(long long)requestedFrame,
				(long long)frameIndex,
				(long long)currentBefore,
				gLastError);
			unlockMutex();
			return 0;
		}
		didSeek = 1;
	}

	int skippedFrames = 0;
	while (1) {
		if (!receiveNextFrame(dec)) {
			if (dec->rgba) {
				if (!copyRgbaCacheToBuffer(dec, dstBuffer, dstLineStride)) {
					setDebug("decode-into eof-cache-copy-failed requested=%lld clamped=%lld currentBefore=%lld error=%s",
						(long long)requestedFrame,
						(long long)frameIndex,
						(long long)currentBefore,
						gLastError);
					unlockMutex();
					return 0;
				}

				outFrame->data = dstBuffer;
				outFrame->width = dec->width;
				outFrame->height = dec->height;
				outFrame->stride = dstLineStride;
				outFrame->frame_index = dec->current_frame;
				setDebug("decode-into eof-return-cache requested=%lld clamped=%lld currentBefore=%lld returned=%lld seek=%d skipped=%d size=%dx%d stride=%d dst=%p sample=%u",
					(long long)requestedFrame,
					(long long)frameIndex,
					(long long)currentBefore,
					(long long)outFrame->frame_index,
					didSeek,
					skippedFrames,
					outFrame->width,
					outFrame->height,
					outFrame->stride,
					(void*)dstBuffer,
					sampleFrameBytes(dec));
				unlockMutex();
				return 1;
			}

			if (gLastError[0] == '\0') {
				setError("No decoded frame available");
			}
			setDebug("decode-into failed-no-frame requested=%lld clamped=%lld currentBefore=%lld seek=%d skipped=%d error=%s",
				(long long)requestedFrame,
				(long long)frameIndex,
				(long long)currentBefore,
				didSeek,
				skippedFrames,
				gLastError);
			unlockMutex();
			return 0;
		}

		int64_t decodedIndex = 0;
		int hasTimestampIndex = estimateFrameIndex(dec, &decodedIndex);
		if (hasTimestampIndex && decodedIndex < frameIndex) {
			dec->current_frame = decodedIndex;
			skippedFrames++;
			continue;
		}
		if (!hasTimestampIndex && decodedIndex < frameIndex) {
			dec->current_frame = decodedIndex;
			skippedFrames++;
			continue;
		}

		if (!convertFrameToBuffer(dec, dstBuffer, dstLineStride)) {
			setDebug("decode-into convert-failed requested=%lld clamped=%lld currentBefore=%lld decoded=%lld hasTimestamp=%d seek=%d skipped=%d error=%s",
				(long long)requestedFrame,
				(long long)frameIndex,
				(long long)currentBefore,
				(long long)decodedIndex,
				hasTimestampIndex,
				didSeek,
				skippedFrames,
				gLastError);
			unlockMutex();
			return 0;
		}

		invalidateRgbaCache(dec);
		dec->current_frame = frameIndex;
		outFrame->data = dstBuffer;
		outFrame->width = dec->width;
		outFrame->height = dec->height;
		outFrame->stride = dstLineStride;
		outFrame->frame_index = frameIndex;
		setDebug("decode-into converted requested=%lld clamped=%lld currentBefore=%lld decoded=%lld hasTimestamp=%d seek=%d skipped=%d returned=%lld size=%dx%d stride=%d swsRows=%d firstA=%d-%d firstA0=%d lastA=%d-%d lastA0=%d firstRowSample=%u lastRowSample=%u dst=%p",
			(long long)requestedFrame,
			(long long)frameIndex,
			(long long)currentBefore,
			(long long)decodedIndex,
			hasTimestampIndex,
			didSeek,
			skippedFrames,
			(long long)outFrame->frame_index,
			outFrame->width,
			outFrame->height,
			outFrame->stride,
			dec->last_scaled_rows,
			dec->first_row_alpha_min,
			dec->first_row_alpha_max,
			dec->first_row_alpha_zero_count,
			dec->last_row_alpha_min,
			dec->last_row_alpha_max,
			dec->last_row_alpha_zero_count,
			dec->first_row_sample,
			dec->last_row_sample,
			(void*)dstBuffer);
		unlockMutex();
		return 1;
	}
}

TLAV_EXPORT void tlav_close(void* handle)
{
	lockMutex();
	closeDecoder((TlavDecoder*)handle);
	unlockMutex();
}
