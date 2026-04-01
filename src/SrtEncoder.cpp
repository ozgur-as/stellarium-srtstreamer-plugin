/*
 * Stellarium SRT Streamer Plugin — FFmpeg encoder + SRT output
 * Copyright (C) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "SrtEncoder.hpp"

#include <QDebug>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}

SrtEncoder::SrtEncoder(QObject* parent)
	: QObject(parent)
{
}

SrtEncoder::~SrtEncoder()
{
	close();
}

QString SrtEncoder::buildSrtUrl(const Config& cfg) const
{
	QString url = cfg.srtUrl;

	// Append mode parameter if not already present
	if (!url.contains("mode="))
	{
		QChar sep = url.contains('?') ? '&' : '?';
		url += sep + QString("mode=%1").arg(cfg.srtMode);
	}

	// SRT options (values in microseconds)
	if (!url.contains("connect_timeout"))
	{
		QChar sep = url.contains('?') ? '&' : '?';
		url += sep + QString("connect_timeout=3000000"); // 3s handshake timeout
	}
	if (!url.contains("transtype"))
	{
		QChar sep = url.contains('?') ? '&' : '?';
		url += sep + QString("transtype=live");
	}
	// Detect dead peers faster (default is 5s, we use 3s)
	if (!url.contains("peeridletimeout"))
	{
		QChar sep = url.contains('?') ? '&' : '?';
		url += sep + QString("peeridletimeout=3000000"); // 3s in microseconds
	}
	// Send timeout — fail fast if the peer is gone
	if (!url.contains("sndtimeo") && cfg.srtMode == "caller")
	{
		QChar sep = url.contains('?') ? '&' : '?';
		url += sep + QString("sndtimeo=2000000"); // 2s send timeout
	}

	return url;
}

bool SrtEncoder::openCodec(const Config& cfg)
{
	// --- Step 1: Determine target codec and pixel format ---
	QString codecName;
	AVPixelFormat pixFmt;

	if (cfg.use10bit)
	{
		// Map H.264 backend choice to 10-bit HEVC equivalent
		if (cfg.encoder == "h264_nvenc")
		{
			codecName = "hevc_nvenc";
			pixFmt    = AV_PIX_FMT_P010LE;
		}
		else if (cfg.encoder == "h264_vaapi")
		{
			codecName = "hevc_vaapi";
			pixFmt    = AV_PIX_FMT_P010LE;
		}
		else
		{
			codecName = "libx265";
			pixFmt    = AV_PIX_FMT_YUV420P10LE;
		}
	}
	else
	{
		codecName = cfg.encoder;
		pixFmt    = AV_PIX_FMT_YUV420P;
	}

	// --- Step 2: Find the encoder, with progressive fallback ---
	const AVCodec* codec = avcodec_find_encoder_by_name(codecName.toUtf8().constData());

	// 10-bit encoder not available → fall back to 8-bit H.264 variant
	if (!codec && cfg.use10bit)
	{
		qWarning() << "[SrtEncoder] 10-bit encoder" << codecName
		           << "not found, falling back to 8-bit H.264";
		codecName = cfg.encoder;
		pixFmt    = AV_PIX_FMT_YUV420P;
		codec     = avcodec_find_encoder_by_name(codecName.toUtf8().constData());
	}

	// Requested encoder not available → fall back to libx264
	if (!codec)
	{
		qWarning() << "[SrtEncoder] Encoder" << codecName
		           << "not found, falling back to libx264";
		codec  = avcodec_find_encoder_by_name("libx264");
		pixFmt = AV_PIX_FMT_YUV420P;
	}

	if (!codec)
	{
		errorMsg = "No suitable encoder found";
		return false;
	}

	// --- Step 3: Allocate and configure codec context ---
	codecCtx = avcodec_alloc_context3(codec);
	if (!codecCtx)
	{
		errorMsg = "Failed to allocate codec context";
		return false;
	}

	codecCtx->width     = cfg.width;
	codecCtx->height    = cfg.height;
	codecCtx->time_base = AVRational{1, cfg.fps};
	codecCtx->framerate = AVRational{cfg.fps, 1};
	codecCtx->pix_fmt   = pixFmt;
	codecCtx->bit_rate  = static_cast<int64_t>(cfg.bitrateKbps) * 1000;
	codecCtx->gop_size  = cfg.fps * 2;  // keyframe every 2 seconds
	codecCtx->max_b_frames = 0;         // reduce latency

	// --- Step 4: Encoder-specific options ---
	QString name(codec->name);
	bool is10bit = (pixFmt == AV_PIX_FMT_P010LE || pixFmt == AV_PIX_FMT_YUV420P10LE);

	if (name == "hevc_nvenc")
	{
		av_opt_set(codecCtx->priv_data, "preset", "p4", 0);    // balanced quality/speed
		av_opt_set(codecCtx->priv_data, "tune",   "ll", 0);    // low latency
		av_opt_set(codecCtx->priv_data, "rc",     "cbr", 0);   // constant bitrate
		if (is10bit)
			av_opt_set(codecCtx->priv_data, "profile", "main10", 0);
	}
	else if (name == "h264_nvenc")
	{
		av_opt_set(codecCtx->priv_data, "preset", "p4", 0);
		av_opt_set(codecCtx->priv_data, "tune",   "ll", 0);
		av_opt_set(codecCtx->priv_data, "rc",     "cbr", 0);
	}
	else if (name == "hevc_vaapi")
	{
		if (is10bit)
			av_opt_set(codecCtx->priv_data, "profile", "main10", 0);
	}
	else if (name == "h264_vaapi")
	{
		// No additional VAAPI-specific options
	}
	else if (name == "libx265")
	{
		av_opt_set(codecCtx->priv_data, "preset", "ultrafast", 0);
		av_opt_set(codecCtx->priv_data, "tune",   "zerolatency", 0);
		if (is10bit)
			av_opt_set(codecCtx->priv_data, "profile", "main10", 0);
	}
	else
	{
		// libx264 or any other CPU fallback
		av_opt_set(codecCtx->priv_data, "preset", "ultrafast", 0);
		av_opt_set(codecCtx->priv_data, "tune",   "zerolatency", 0);
	}

	// Set flags for streaming
	codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;

	// --- Step 5: Open the codec, with fallback on failure ---
	int ret = avcodec_open2(codecCtx, codec, nullptr);
	if (ret < 0)
	{
		char errbuf[256];
		av_strerror(ret, errbuf, sizeof(errbuf));
		qWarning() << "[SrtEncoder]" << codec->name << "failed:" << errbuf;

		// Try falling back to libx264 (8-bit) if we weren't already using it
		if (name != "libx264")
		{
			qWarning() << "[SrtEncoder] Falling back to libx264 (8-bit)";
			avcodec_free_context(&codecCtx);

			const AVCodec* fallback = avcodec_find_encoder_by_name("libx264");
			if (!fallback)
			{
				errorMsg = "Encoder failed and libx264 not available";
				return false;
			}

			codecCtx = avcodec_alloc_context3(fallback);
			codecCtx->width     = cfg.width;
			codecCtx->height    = cfg.height;
			codecCtx->time_base = AVRational{1, cfg.fps};
			codecCtx->framerate = AVRational{cfg.fps, 1};
			codecCtx->pix_fmt   = AV_PIX_FMT_YUV420P;
			codecCtx->bit_rate  = static_cast<int64_t>(cfg.bitrateKbps) * 1000;
			codecCtx->gop_size  = cfg.fps * 2;
			codecCtx->max_b_frames = 0;
			codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
			av_opt_set(codecCtx->priv_data, "preset", "ultrafast", 0);
			av_opt_set(codecCtx->priv_data, "tune",   "zerolatency", 0);

			if (avcodec_open2(codecCtx, fallback, nullptr) < 0)
			{
				errorMsg = "Failed to open fallback libx264 encoder";
				avcodec_free_context(&codecCtx);
				return false;
			}
			codec = fallback;
		}
		else
		{
			errorMsg = QString("Failed to open encoder: %1").arg(codec->name);
			return false;
		}
	}

	const char* depth = (codecCtx->pix_fmt == AV_PIX_FMT_P010LE ||
	                      codecCtx->pix_fmt == AV_PIX_FMT_YUV420P10LE) ? "10-bit" : "8-bit";
	qDebug() << "[SrtEncoder] Opened codec:" << codec->name << depth
	         << cfg.width << "x" << cfg.height << "@" << cfg.fps << "fps"
	         << cfg.bitrateKbps << "kbps";

	return true;
}

static int interruptCallback(void* opaque)
{
	auto* encoder = static_cast<SrtEncoder*>(opaque);
	return encoder->isStopRequested() ? 1 : 0;
}

bool SrtEncoder::openOutput(const Config& cfg)
{
	QString url = buildSrtUrl(cfg);
	QByteArray urlBytes = url.toUtf8();

	int ret = avformat_alloc_output_context2(&fmtCtx, nullptr, "mpegts", urlBytes.constData());
	if (ret < 0 || !fmtCtx)
	{
		errorMsg = "Failed to allocate output context for MPEG-TS/SRT";
		return false;
	}

	// Set interrupt callback so blocking I/O can be cancelled
	fmtCtx->interrupt_callback.callback = interruptCallback;
	fmtCtx->interrupt_callback.opaque   = this;

	stream = avformat_new_stream(fmtCtx, nullptr);
	if (!stream)
	{
		errorMsg = "Failed to create output stream";
		avformat_free_context(fmtCtx);
		fmtCtx = nullptr;
		return false;
	}

	avcodec_parameters_from_context(stream->codecpar, codecCtx);
	stream->time_base = codecCtx->time_base;

	// Open SRT output — use avio_open2 with interrupt callback
	// so the connection can be cancelled via stopRequested
	AVIOInterruptCB ioCb;
	ioCb.callback = interruptCallback;
	ioCb.opaque   = this;
	ret = avio_open2(&fmtCtx->pb, urlBytes.constData(), AVIO_FLAG_WRITE, &ioCb, nullptr);
	if (ret < 0)
	{
		char errbuf[256];
		av_strerror(ret, errbuf, sizeof(errbuf));
		errorMsg = QString("Failed to open SRT output (%1): %2").arg(url, errbuf);
		stream = nullptr;
		avformat_free_context(fmtCtx);
		fmtCtx = nullptr;
		return false;
	}

	ret = avformat_write_header(fmtCtx, nullptr);
	if (ret < 0)
	{
		char errbuf[256];
		av_strerror(ret, errbuf, sizeof(errbuf));
		errorMsg = QString("Failed to write stream header: %1").arg(errbuf);
		avio_closep(&fmtCtx->pb);
		stream = nullptr;
		avformat_free_context(fmtCtx);
		fmtCtx = nullptr;
		return false;
	}

	qDebug() << "[SrtEncoder] SRT output opened:" << url;
	return true;
}

bool SrtEncoder::open(const Config& cfg)
{
	if (opened.load())
		close();

	stopRequested.store(false);

	// Open codec
	if (!openCodec(cfg))
		return false;

	// Open SRT output
	if (!openOutput(cfg))
	{
		avcodec_free_context(&codecCtx);
		return false;
	}

	// Allocate YUV frame
	yuvFrame = av_frame_alloc();
	yuvFrame->format = codecCtx->pix_fmt;
	yuvFrame->width  = cfg.width;
	yuvFrame->height = cfg.height;
	av_frame_get_buffer(yuvFrame, 32);

	// Allocate packet
	pkt = av_packet_alloc();

	// Setup swscale: RGBA → YUV (with vertical flip for OpenGL's bottom-up rows)
	swsCtx = sws_getContext(
		cfg.width, cfg.height, AV_PIX_FMT_RGBA,
		cfg.width, cfg.height, codecCtx->pix_fmt,
		SWS_BILINEAR, nullptr, nullptr, nullptr);

	if (!swsCtx)
	{
		errorMsg = "Failed to create swscale context";
		close();
		return false;
	}

	// Allocate double-buffers
	size_t bufSize = static_cast<size_t>(cfg.width) * cfg.height * 4;
	for (int i = 0; i < NUM_BUFFERS; ++i)
	{
		buffers[i].data   = std::make_unique<uint8_t[]>(bufSize);
		buffers[i].width  = cfg.width;
		buffers[i].height = cfg.height;
		buffers[i].ready  = false;
	}
	writeIdx = 0;
	readIdx  = 0;

	pts = 0;
	stopRequested.store(false);
	opened.store(true);

	// Start encoder thread.
	// We use a direct connection so encoderLoop() runs on the worker thread
	// when it emits 'started'. Disconnect first to avoid accumulating connections.
	QObject::disconnect(&workerThread, &QThread::started, this, nullptr);
	QObject::connect(&workerThread, &QThread::started,
	                 this, &SrtEncoder::encoderLoop, Qt::DirectConnection);
	workerThread.start();

	return true;
}

void SrtEncoder::close()
{
	// Always set stop flag first — this triggers the interrupt callback
	// to cancel any blocking avio_open/SRT connection in progress
	stopRequested.store(true);

	if (!opened.load())
		return;

	frameAvailable.wakeAll();

	workerThread.quit();
	workerThread.wait(5000);

	// Flush encoder
	flushEncoder();

	// Write trailer
	if (fmtCtx && fmtCtx->pb)
	{
		av_write_trailer(fmtCtx);
		avio_closep(&fmtCtx->pb);
	}

	// Free resources
	if (swsCtx)    { sws_freeContext(swsCtx);        swsCtx   = nullptr; }
	if (yuvFrame)  { av_frame_free(&yuvFrame);        yuvFrame = nullptr; }
	if (pkt)       { av_packet_free(&pkt);             pkt      = nullptr; }
	if (codecCtx)  { avcodec_free_context(&codecCtx);  codecCtx = nullptr; }
	if (fmtCtx)    { avformat_free_context(fmtCtx);    fmtCtx   = nullptr; }

	stream = nullptr;

	for (int i = 0; i < NUM_BUFFERS; ++i)
	{
		buffers[i].data.reset();
		buffers[i].ready = false;
	}

	opened.store(false);
	qDebug() << "[SrtEncoder] Closed.";
}

void SrtEncoder::submitFrame(const uint8_t* rgba, int width, int height)
{
	if (!opened.load() || !rgba)
		return;

	QMutexLocker lock(&mutex);

	FrameBuffer& buf = buffers[writeIdx];
	size_t expected = static_cast<size_t>(buf.width) * buf.height * 4;
	size_t incoming = static_cast<size_t>(width) * height * 4;

	if (incoming != expected)
	{
		// Resolution mismatch — skip this frame.
		// The encoder was opened with a fixed resolution.
		return;
	}

	memcpy(buf.data.get(), rgba, incoming);
	buf.ready = true;

	writeIdx = (writeIdx + 1) % NUM_BUFFERS;

	frameAvailable.wakeOne();
}

void SrtEncoder::encoderLoop()
{
	qDebug() << "[SrtEncoder] Encoder thread started.";

	while (!stopRequested.load())
	{
		QMutexLocker lock(&mutex);

		// Wait for a frame
		while (!buffers[readIdx].ready && !stopRequested.load())
		{
			frameAvailable.wait(&mutex, 100); // 100ms timeout to check stop flag
		}

		if (stopRequested.load())
			break;

		FrameBuffer& buf = buffers[readIdx];
		if (!buf.ready)
			continue;

		int w = buf.width;
		int h = buf.height;

		// Convert RGBA (bottom-up) → YUV (8-bit or 10-bit depending on codec)
		// OpenGL gives us bottom-to-top rows. We flip by using a negative
		// stride and pointing to the last row.
		const uint8_t* srcSlice[1] = { buf.data.get() + (h - 1) * w * 4 };
		int srcStride[1] = { -(w * 4) };  // negative = flip vertically

		av_frame_make_writable(yuvFrame);
		sws_scale(swsCtx,
		          srcSlice, srcStride, 0, h,
		          yuvFrame->data, yuvFrame->linesize);

		yuvFrame->pts = pts++;

		buf.ready = false;
		readIdx = (readIdx + 1) % NUM_BUFFERS;

		lock.unlock();

		// Encode and write
		encodeAndWrite(yuvFrame);
	}

	qDebug() << "[SrtEncoder] Encoder thread stopped.";
}

void SrtEncoder::encodeAndWrite(AVFrame* frame)
{
	if (!codecCtx || !fmtCtx || !stream || !pkt)
		return;

	int ret = avcodec_send_frame(codecCtx, frame);
	if (ret < 0)
	{
		char errbuf[256];
		av_strerror(ret, errbuf, sizeof(errbuf));
		stopRequested.store(true);
		emit errorOccurred(QString("Encoding error: %1").arg(errbuf));
		return;
	}

	while (ret >= 0)
	{
		ret = avcodec_receive_packet(codecCtx, pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			break;
		if (ret < 0)
		{
			char errbuf[256];
			av_strerror(ret, errbuf, sizeof(errbuf));
			stopRequested.store(true);
			emit errorOccurred(QString("Receive packet error: %1").arg(errbuf));
			break;
		}

		av_packet_rescale_ts(pkt, codecCtx->time_base, stream->time_base);
		pkt->stream_index = stream->index;

		ret = av_interleaved_write_frame(fmtCtx, pkt);
		av_packet_unref(pkt);

		if (ret < 0)
		{
			char errbuf[256];
			av_strerror(ret, errbuf, sizeof(errbuf));
			stopRequested.store(true);
			emit errorOccurred(QString("Write frame error: %1").arg(errbuf));
			return;
		}
	}
}

void SrtEncoder::flushEncoder()
{
	if (!codecCtx)
		return;

	avcodec_send_frame(codecCtx, nullptr); // signal flush

	while (true)
	{
		int ret = avcodec_receive_packet(codecCtx, pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			break;
		if (ret < 0)
			break;

		if (fmtCtx && stream)
		{
			av_packet_rescale_ts(pkt, codecCtx->time_base, stream->time_base);
			pkt->stream_index = stream->index;
			av_interleaved_write_frame(fmtCtx, pkt);
		}
		av_packet_unref(pkt);
	}
}

QString SrtEncoder::lastError() const
{
	return errorMsg;
}
