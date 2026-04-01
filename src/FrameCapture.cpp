/*
 * Stellarium SRT Streamer Plugin — OpenGL framebuffer capture
 * Copyright (C) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "FrameCapture.hpp"
#include <QOpenGLContext>
#include <QDebug>

// SSE2 is always available on x86-64
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#include <emmintrin.h>
#define SRT_HAS_SSE2 1
#else
#define SRT_HAS_SSE2 0
#endif

FrameCapture::FrameCapture() = default;

FrameCapture::~FrameCapture()
{
	deinit();
}

void FrameCapture::init()
{
	if (initialized)
		return;

	initializeOpenGLFunctions();

	// Check for PBO support (available in OpenGL 2.1+ / ES 3.0+)
	QOpenGLContext* ctx = QOpenGLContext::currentContext();
	if (ctx)
	{
		auto fmt = ctx->format();
		bool isES = ctx->isOpenGLES();
		int major = fmt.majorVersion();

		// PBOs are available in GL 2.1+ or GLES 3.0+
		usePBO = isES ? (major >= 3) : (major >= 2);
	}

	if (usePBO)
	{
		glGenBuffers(2, pbo);
		qDebug() << "[SrtStreamer] FrameCapture: using PBO double-buffering";
	}
	else
	{
		qDebug() << "[SrtStreamer] FrameCapture: PBO not available, using glReadPixels fallback";
	}

	initialized = true;
}

void FrameCapture::deinit()
{
	if (!initialized)
		return;

	if (usePBO)
	{
		glDeleteBuffers(2, pbo);
		pbo[0] = pbo[1] = 0;
	}

	cpuBuffer.clear();
	initialized = false;
	firstCapture = true;
	currentWidth = currentHeight = 0;
}

void FrameCapture::ensureBufferSize(int width, int height)
{
	if (width == currentWidth && height == currentHeight)
		return;

	size_t size = static_cast<size_t>(width) * height * 4;

	if (usePBO)
	{
		for (int i = 0; i < 2; ++i)
		{
			glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[i]);
			glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(size),
			             nullptr, GL_STREAM_READ);
		}
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		firstCapture = true;
	}

	cpuBuffer.resize(size);

	currentWidth  = width;
	currentHeight = height;
}

// 4×4 Bayer ordered dither — breaks up 8-bit quantization banding.
//
// Adds +/- 0.5 LSB of spatially-patterned noise to R, G, B channels
// (alpha untouched).  With float-to-int truncation, negative Bayer
// entries subtract 1 from the pixel and positive entries leave it
// unchanged.  This is deterministic (no temporal flicker) and
// compresses well with video codecs.
//
// SSE2 fast path: one saturating subtract per 4 pixels (16 bytes).
static void applyBayerDither(uint8_t* rgba, int width, int height)
{
	// 4×4 Bayer threshold matrix, normalized to [-0.5, +0.5]
	static const float bayer4x4[4][4] = {
		{ -0.46875f,  0.03125f, -0.34375f,  0.15625f },
		{  0.28125f, -0.21875f,  0.40625f, -0.09375f },
		{ -0.28125f,  0.21875f, -0.40625f,  0.09375f },
		{  0.46875f, -0.03125f,  0.34375f, -0.15625f }
	};

#if SRT_HAS_SSE2
	// Which Bayer entries are negative per row (sign pattern):
	//   Row 0,2: negative at pixel x%4 = 0, 2
	//   Row 1,3: negative at pixel x%4 = 1, 3
	// Masks: subtract 1 from R,G,B at those positions, leave A alone.
	static const __m128i maskEven = _mm_setr_epi8( // rows 0, 2
		1,1,1,0,  0,0,0,0,  1,1,1,0,  0,0,0,0);
	static const __m128i maskOdd  = _mm_setr_epi8( // rows 1, 3
		0,0,0,0,  1,1,1,0,  0,0,0,0,  1,1,1,0);

	for (int y = 0; y < height; ++y)
	{
		uint8_t* row = rgba + static_cast<size_t>(y) * width * 4;
		const __m128i mask = (y & 1) ? maskOdd : maskEven;
		const int simdWidth = (width / 4) * 4;

		for (int x = 0; x < simdWidth; x += 4)
		{
			__m128i px = _mm_loadu_si128(
				reinterpret_cast<const __m128i*>(row + x * 4));
			px = _mm_subs_epu8(px, mask);
			_mm_storeu_si128(
				reinterpret_cast<__m128i*>(row + x * 4), px);
		}

		// Scalar tail (only when width is not a multiple of 4)
		for (int x = simdWidth; x < width; ++x)
		{
			float d = bayer4x4[y & 3][x & 3];
			uint8_t* px = row + x * 4;
			px[0] = static_cast<uint8_t>(static_cast<int>(px[0] + d));
			px[1] = static_cast<uint8_t>(static_cast<int>(px[1] + d));
			px[2] = static_cast<uint8_t>(static_cast<int>(px[2] + d));
		}
	}
#else
	// Scalar fallback for non-x86 platforms
	for (int y = 0; y < height; ++y)
	{
		uint8_t* row = rgba + static_cast<size_t>(y) * width * 4;
		for (int x = 0; x < width; ++x)
		{
			float d = bayer4x4[y & 3][x & 3];
			uint8_t* px = row + x * 4;
			px[0] = static_cast<uint8_t>(static_cast<int>(px[0] + d));
			px[1] = static_cast<uint8_t>(static_cast<int>(px[1] + d));
			px[2] = static_cast<uint8_t>(static_cast<int>(px[2] + d));
		}
	}
#endif
}

const uint8_t* FrameCapture::capture(GLuint fbo, int width, int height)
{
	if (!initialized || width <= 0 || height <= 0)
		return nullptr;

	ensureBufferSize(width, height);

	// Bind the target framebuffer for reading
	GLint prevReadFbo = 0;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);

	if (usePBO)
	{
		// PBO double-buffer path:
		// Frame N:   start async read into PBO[write]
		// Frame N+1: map PBO[read] (which was PBO[write] last frame)
		//
		// This overlaps the GPU→CPU transfer with encoding of the
		// previous frame.

		int writeSlot = pboWriteIndex;
		int readSlot  = 1 - pboWriteIndex;

		// Start async readback into the write PBO
		glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[writeSlot]);
		glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

		if (firstCapture)
		{
			// First frame: no previous readback to map yet.
			// Do a synchronous fallback this one time.
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
			glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, cpuBuffer.data());
			glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));

			firstCapture = false;
			pboWriteIndex = readSlot;
			applyBayerDither(cpuBuffer.data(), width, height);
			return cpuBuffer.data();
		}

		// Map the read PBO (contains the previous frame's readback)
		glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[readSlot]);
		void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
		                                static_cast<GLsizeiptr>(cpuBuffer.size()),
		                                GL_MAP_READ_BIT);

		if (mapped)
		{
			memcpy(cpuBuffer.data(), mapped, cpuBuffer.size());
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		}
		else
		{
			// Fallback: synchronous read
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
			glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, cpuBuffer.data());
		}

		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		pboWriteIndex = readSlot;
	}
	else
	{
		// Simple synchronous path
		glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, cpuBuffer.data());
	}

	glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
	applyBayerDither(cpuBuffer.data(), width, height);
	return cpuBuffer.data();
}
