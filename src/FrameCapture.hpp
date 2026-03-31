/*
 * Stellarium SRT Streamer Plugin — OpenGL framebuffer capture
 * Copyright (C) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef FRAMECAPTURE_HPP
#define FRAMECAPTURE_HPP

#include <QOpenGLExtraFunctions>
#include <cstdint>
#include <memory>
#include <vector>

/// Captures the current OpenGL framebuffer contents into a CPU-side
/// RGBA buffer suitable for passing to SrtEncoder::submitFrame().
///
/// Follows the same pattern as Stellarium's SpoutSender: it can read
/// from the default framebuffer or from a specific FBO.
///
/// Uses PBO (Pixel Buffer Object) double-buffering when available to
/// overlap GPU readback with CPU encoding.
class FrameCapture : protected QOpenGLExtraFunctions
{
public:
	FrameCapture();
	~FrameCapture();

	/// Initialize OpenGL resources. Must be called with a current GL context.
	void init();

	/// Release OpenGL resources.
	void deinit();

	/// Capture the current framebuffer.
	/// @param fbo    The framebuffer object to read from (0 = default).
	/// @param width  Viewport width.
	/// @param height Viewport height.
	/// @return Pointer to the RGBA pixel data (valid until next capture call).
	///         Rows are bottom-to-top (OpenGL convention). Returns nullptr on failure.
	const uint8_t* capture(GLuint fbo, int width, int height);

	/// Returns the width of the last captured frame.
	int lastWidth() const { return currentWidth; }

	/// Returns the height of the last captured frame.
	int lastHeight() const { return currentHeight; }

private:
	void ensureBufferSize(int width, int height);

	bool initialized = false;
	int currentWidth  = 0;
	int currentHeight = 0;

	// PBO double-buffering
	bool usePBO = false;
	GLuint pbo[2] = {0, 0};
	int pboWriteIndex = 0;
	bool firstCapture = true;

	// Fallback CPU buffer (used when PBOs are not available)
	std::vector<uint8_t> cpuBuffer;
};

#endif // FRAMECAPTURE_HPP
