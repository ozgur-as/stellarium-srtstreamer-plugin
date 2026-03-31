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
	return cpuBuffer.data();
}
