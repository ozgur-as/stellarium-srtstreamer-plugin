/*
 * Stellarium SRT Streamer Plugin — FFmpeg encoder + SRT output
 * Copyright (C) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef SRTENCODER_HPP
#define SRTENCODER_HPP

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QString>

#include <cstdint>
#include <atomic>
#include <memory>

// Forward-declare FFmpeg types to keep this header clean.
// The implementation includes the actual FFmpeg headers.
struct AVCodecContext;
struct AVFormatContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct SwsContext;

/// Encodes RGBA frames to H.264/HEVC and streams them over SRT.
///
/// Encoding runs on a dedicated worker thread. The main (GL) thread
/// calls submitFrame() which copies pixel data into a double-buffer
/// and signals the worker.
class SrtEncoder : public QObject
{
	Q_OBJECT

public:
	struct Config
	{
		QString srtUrl     = "srt://127.0.0.1:9000";
		QString srtMode    = "caller";     // "listener" or "caller"
		QString encoder    = "libx264";    // codec name
		int     width      = 1920;
		int     height     = 1080;
		int     fps        = 30;
		int     bitrateKbps = 6000;
	};

	explicit SrtEncoder(QObject* parent = nullptr);
	~SrtEncoder() override;

	/// Open the encoder and SRT output. Returns true on success.
	bool open(const Config& cfg);

	/// Close the encoder and SRT output. Safe to call even if not open.
	void close();

	bool isOpen() const { return opened.load(); }

	/// Submit an RGBA frame for encoding. The data is copied internally,
	/// so the caller's buffer can be reused immediately.
	/// @param rgba     Pointer to width*height*4 bytes of RGBA pixel data.
	///                 Rows are bottom-to-top (OpenGL convention).
	/// @param width    Frame width in pixels.
	/// @param height   Frame height in pixels.
	void submitFrame(const uint8_t* rgba, int width, int height);

	/// Returns a human-readable description of the last error.
	QString lastError() const;

	/// Used by the interrupt callback to cancel blocking I/O.
	bool isStopRequested() const { return stopRequested.load(); }

	/// Request cancellation of any in-progress connection or encoding.
	void requestStop() { stopRequested.store(true); }

signals:
	void errorOccurred(const QString& message);

private slots:
	void encoderLoop();

private:
	bool openCodec(const Config& cfg);
	bool openOutput(const Config& cfg);
	void encodeAndWrite(AVFrame* frame);
	void flushEncoder();
	QString buildSrtUrl(const Config& cfg) const;

	// FFmpeg state
	AVCodecContext*  codecCtx  = nullptr;
	AVFormatContext* fmtCtx    = nullptr;
	AVStream*        stream    = nullptr;
	SwsContext*      swsCtx    = nullptr;
	AVFrame*         yuvFrame  = nullptr;
	AVPacket*        pkt       = nullptr;

	int64_t pts = 0;

	// Double-buffer for frame handoff (GL thread → encoder thread)
	static constexpr int NUM_BUFFERS = 2;
	struct FrameBuffer
	{
		std::unique_ptr<uint8_t[]> data;
		int width  = 0;
		int height = 0;
		bool ready = false;
	};
	FrameBuffer buffers[NUM_BUFFERS];
	int writeIdx = 0;   // GL thread writes here
	int readIdx  = 0;   // encoder thread reads here

	QMutex mutex;
	QWaitCondition frameAvailable;

	// Worker thread
	QThread workerThread;
	std::atomic<bool> opened{false};
	std::atomic<bool> stopRequested{false};

	QString errorMsg;
};

#endif // SRTENCODER_HPP
