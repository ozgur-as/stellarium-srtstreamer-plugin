/*
 * Stellarium SRT Streamer Plugin
 * Copyright (C) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "SrtStreamerModule.hpp"
#include "SrtEncoder.hpp"
#include "FrameCapture.hpp"
#include "gui/SrtStreamerWindow.hpp"

#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelModuleMgr.hpp"
#include "StelProjector.hpp"
#include "StelGui.hpp"
#include "StelGuiItems.hpp"
#include "SkyGui.hpp"

#include <QSettings>
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QLabel>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QtConcurrent/QtConcurrent>
#include <QTimer>

// ---- Plugin interface ----

StelModule* SrtStreamerPluginInterface::getStelModule() const
{
	return new SrtStreamerModule();
}

StelPluginInfo SrtStreamerPluginInterface::getPluginInfo() const
{
	StelPluginInfo info;
	info.id          = "SrtStreamer";
	info.displayedName = N_("SRT Video Streamer");
	info.authors     = "Ozgur As";
	info.contact     = "https://github.com/ozgur-as/stellarium-srtstreamer-plugin";
	info.description = N_("Captures the rendered sky framebuffer and streams it "
	                       "over SRT (Secure Reliable Transport) as encoded H.264 "
	                       "video.");
	info.version     = SRTSTREAMER_VERSION;
	info.license     = "GPLv2+";
	return info;
}

// ---- Module implementation ----

SrtStreamerModule::SrtStreamerModule()
{
	setObjectName("SrtStreamer");
}

SrtStreamerModule::~SrtStreamerModule() = default;

void SrtStreamerModule::init()
{
	Q_INIT_RESOURCE(SrtStreamer);
	loadSettings();

	frameCapture = std::make_unique<FrameCapture>();
	frameCapture->init();

	encoder = std::make_unique<SrtEncoder>();
	connect(encoder.get(), &SrtEncoder::errorOccurred, this, [this](const QString& msg) {
		qWarning() << "[SrtStreamer] Encoder error:" << msg;
		QMetaObject::invokeMethod(this, [this, msg]() {
			// streaming is still true here so stopStreaming() won't bail out
			stopStreaming();
			emit errorMessage(QString("Connection lost: %1").arg(msg));
		}, Qt::QueuedConnection);
	});

	addAction("actionToggleSrtStreaming",
	          N_("SRT Streamer"),
	          N_("Toggle SRT streaming"),
	          "srtActive",
	          "Ctrl+Shift+S");

	addAction("actionShow_SrtStreamer_dialog",
	          N_("SRT Streamer"),
	          N_("SRT Streamer settings"),
	          this, "showConfigDialog()",
	          "Ctrl+Shift+R");

	// Add toolbar button
	try
	{
		StelGui* gui = dynamic_cast<StelGui*>(StelApp::getInstance().getGui());
		if (gui)
		{
			StelButton* btn = new StelButton(Q_NULLPTR,
				QPixmap(":/SrtStreamer/bt_srt_on.png"),
				QPixmap(":/SrtStreamer/bt_srt_off.png"),
				QPixmap(":/graphicGui/miscGlow32x32.png"),
				"actionToggleSrtStreaming",
				false,
				"actionShow_SrtStreamer_dialog");
			gui->getButtonBar()->addButton(btn, "065-pluginsGroup");
		}
	}
	catch (std::runtime_error& e)
	{
		qWarning() << "[SrtStreamer] Unable to create toolbar button:" << e.what();
	}

	// Create on-screen status label (Qt widget layer — not captured in stream)
	try
	{
		StelGui* gui = dynamic_cast<StelGui*>(StelApp::getInstance().getGui());
		if (gui && gui->getSkyGui())
		{
			statusLabel = new QLabel();
			statusLabel->setStyleSheet(
				"QLabel { color: #aaaaaa; background: transparent; padding: 2px 6px; font-size: 11px; }");
			statusLabel->setVisible(false);
			statusProxy = gui->getSkyGui()->scene()->addWidget(statusLabel);
			statusProxy->setZValue(100);
			statusProxy->setPos(10, 4);
		}
	}
	catch (std::runtime_error&) {}

	connect(this, &SrtStreamerModule::streamingStateChanged, this, &SrtStreamerModule::updateStatusLabel);
	connect(this, &SrtStreamerModule::connectingStateChanged, this, &SrtStreamerModule::updateStatusLabel);

	qDebug() << "[SrtStreamer] Plugin initialized. Version" << SRTSTREAMER_VERSION;
}

void SrtStreamerModule::deinit()
{
	stopStreaming();
	if (frameCapture)
		frameCapture->deinit();
	encoder.reset();
	frameCapture.reset();

	saveSettings();
}

void SrtStreamerModule::update(double deltaTime)
{
	if (streaming)
		timeSinceLastFrame += deltaTime;
}

void SrtStreamerModule::draw(StelCore* core)
{
	if (!streaming || !encoder || !encoder->isOpen() || !frameCapture)
		return;

	// Frame-rate cap: skip if not enough time has passed
	double frameInterval = 1.0 / static_cast<double>(frameRateCap);
	if (timeSinceLastFrame < frameInterval)
		return;
	timeSinceLastFrame = 0.0;

	// Determine viewport size
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	int vpWidth  = viewport[2];
	int vpHeight = viewport[3];

	if (vpWidth <= 0 || vpHeight <= 0)
		return;

	// Get the current FBO (Stellarium renders to an FBO, not the default framebuffer)
	GLint currentFbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFbo);

	// Capture the framebuffer
	const uint8_t* pixels = frameCapture->capture(
		static_cast<GLuint>(currentFbo), vpWidth, vpHeight);

	if (!pixels)
		return;

	// Determine output dimensions
	int outW = useNativeResolution ? vpWidth  : outputWidth;
	int outH = useNativeResolution ? vpHeight : outputHeight;

	// Submit to encoder (this copies the data and returns immediately)
	encoder->submitFrame(pixels, outW, outH);
}

double SrtStreamerModule::getCallOrder(StelModuleActionName actionName) const
{
	if (actionName == StelModule::ActionDraw)
	{
		// Draw (capture) after everything else. Use a very high value.
		return 10000.0;
	}
	return 0.0;
}

bool SrtStreamerModule::configureGui(bool show)
{
	if (show)
	{
		if (!configWindow)
		{
			configWindow = new SrtStreamerWindow();
			configWindow->setModule(this);
		}
		configWindow->setVisible(true);
	}
	else if (configWindow)
	{
		configWindow->setVisible(false);
	}
	return true;
}

// ---- Settings ----

void SrtStreamerModule::loadSettings()
{
	QSettings* conf = StelApp::getInstance().getSettings();
	Q_ASSERT(conf);

	conf->beginGroup("SrtStreamer");
	srtUrl              = conf->value("srt_url", "srt://127.0.0.1:9000").toString();
	srtMode             = conf->value("srt_mode", "caller").toString();
	encoderName         = conf->value("encoder", "libx264").toString();
	bitrate             = conf->value("bitrate_kbps", 6000).toInt();
	outputWidth         = conf->value("output_width", 1920).toInt();
	outputHeight        = conf->value("output_height", 1080).toInt();
	useNativeResolution = conf->value("use_native_resolution", true).toBool();
	frameRateCap        = conf->value("framerate_cap", 30).toInt();
	conf->endGroup();
}

void SrtStreamerModule::saveSettings()
{
	QSettings* conf = StelApp::getInstance().getSettings();
	Q_ASSERT(conf);

	conf->beginGroup("SrtStreamer");
	conf->setValue("srt_url", srtUrl);
	conf->setValue("srt_mode", srtMode);
	conf->setValue("encoder", encoderName);
	conf->setValue("bitrate_kbps", bitrate);
	conf->setValue("output_width", outputWidth);
	conf->setValue("output_height", outputHeight);
	conf->setValue("use_native_resolution", useNativeResolution);
	conf->setValue("framerate_cap", frameRateCap);
	conf->endGroup();

	conf->sync();
}

// ---- Streaming control ----

void SrtStreamerModule::startStreaming()
{
	if (streaming || connecting)
		return;

	SrtEncoder::Config cfg;
	cfg.srtUrl      = srtUrl;
	cfg.srtMode     = srtMode;
	cfg.encoder     = encoderName;
	cfg.bitrateKbps = bitrate;
	cfg.fps         = frameRateCap;

	if (useNativeResolution)
	{
		// Use Stellarium's view size (safe to call outside GL context)
		const StelCore* core = StelApp::getInstance().getCore();
		int w = static_cast<int>(core->getProjection(StelCore::FrameJ2000)->getViewportWidth());
		int h = static_cast<int>(core->getProjection(StelCore::FrameJ2000)->getViewportHeight());
		cfg.width  = w > 0 ? w : 1920;
		cfg.height = h > 0 ? h : 1080;
	}
	else
	{
		cfg.width  = outputWidth;
		cfg.height = outputHeight;
	}

	// Ensure even dimensions (required by most encoders)
	cfg.width  &= ~1;
	cfg.height &= ~1;

	// Run the blocking SRT connection on a background thread
	// so the UI stays responsive
	connecting = true;
	emit connectingStateChanged(true);
	qDebug() << "[SrtStreamer] Connecting to" << srtUrl << "...";

	// Watchdog: auto-cancel after 5 seconds if still connecting
	QTimer::singleShot(5000, this, [this]() {
		if (connecting)
		{
			qWarning() << "[SrtStreamer] Connection timed out.";
			stopStreaming();
			emit errorMessage(q_("SRT connection timed out. Check the URL and ensure the remote side is running."));
		}
	});

	QtConcurrent::run([this, cfg]() {
		bool ok = encoder->open(cfg);
		QMetaObject::invokeMethod(this, [this, ok]() {
			if (!connecting)
			{
				// User cancelled or timed out while we were connecting
				if (ok)
					encoder->close();
				qDebug() << "[SrtStreamer] Connection cancelled.";
				emit streamingStateChanged(false);
				return;
			}
			connecting = false;
			emit connectingStateChanged(false);
			if (ok)
			{
				streaming = true;
				timeSinceLastFrame = 0.0;
				qDebug() << "[SrtStreamer] Streaming started.";
				emit streamingStateChanged(true);
			}
			else
			{
				qWarning() << "[SrtStreamer] Failed to start:" << encoder->lastError();
				emit errorMessage(encoder->lastError());
				emit streamingStateChanged(false);
			}
		}, Qt::QueuedConnection);
	});
}

void SrtStreamerModule::stopStreaming()
{
	if (!streaming && !connecting)
		return;

	streaming = false;
	connecting = false;

	// Signal the encoder to abort any blocking I/O immediately
	if (encoder)
	{
		encoder->requestStop();
		encoder->close();
	}

	qDebug() << "[SrtStreamer] Streaming stopped.";
	emit streamingStateChanged(false);
}

void SrtStreamerModule::toggleStreaming()
{
	if (streaming || connecting)
		stopStreaming();
	else
		startStreaming();
}

void SrtStreamerModule::showConfigDialog()
{
	configureGui(true);
}

void SrtStreamerModule::updateStatusLabel()
{
	if (!statusLabel)
		return;

	if (streaming)
	{
		statusLabel->setStyleSheet(
			"QLabel { color: #44dd44; background: transparent; padding: 2px 6px; font-size: 11px; }");
		statusLabel->setText(QString("SRT: Streaming"));
		statusLabel->setVisible(true);
	}
	else if (connecting)
	{
		statusLabel->setStyleSheet(
			"QLabel { color: #ddaa00; background: transparent; padding: 2px 6px; font-size: 11px; }");
		statusLabel->setText(QString("SRT: Connecting..."));
		statusLabel->setVisible(true);
	}
	else
	{
		statusLabel->setVisible(false);
	}
}

// ---- Setters ----

void SrtStreamerModule::setSrtUrl(const QString& url)        { srtUrl = url; }
void SrtStreamerModule::setSrtMode(const QString& mode)      { srtMode = mode; }
void SrtStreamerModule::setEncoderName(const QString& name)   { encoderName = name; }
void SrtStreamerModule::setBitrate(int kbps)                  { bitrate = kbps; }
void SrtStreamerModule::setFrameRateCap(int fps)              { frameRateCap = fps; }
void SrtStreamerModule::setUseNativeResolution(bool native)   { useNativeResolution = native; }

void SrtStreamerModule::setOutputResolution(int w, int h)
{
	outputWidth = w;
	outputHeight = h;
}
