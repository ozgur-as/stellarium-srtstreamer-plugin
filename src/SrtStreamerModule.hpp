/*
 * Stellarium SRT Streamer Plugin
 * Copyright (C) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef SRTSTREAMERMODULE_HPP
#define SRTSTREAMERMODULE_HPP

#ifndef SRTSTREAMER_VERSION
#define SRTSTREAMER_VERSION "1.0.0"
#endif

#include "StelModule.hpp"
#include "StelPluginInterface.hpp"

#include <QFont>
#include <memory>

class SrtStreamerWindow;
class SrtEncoder;
class FrameCapture;
class StelCore;

class SrtStreamerModule : public StelModule
{
	Q_OBJECT
	Q_PROPERTY(bool srtActive READ isActive WRITE setActive NOTIFY streamingStateChanged)

public:
	SrtStreamerModule();
	~SrtStreamerModule() override;

	// StelModule interface
	void init() override;
	void deinit() override;
	void update(double deltaTime) override;
	void draw(StelCore* core) override;
	double getCallOrder(StelModuleActionName actionName) const override;
	bool configureGui(bool show) override;

	// --- Public API for the config GUI ---

	bool isStreaming() const { return streaming; }
	bool isConnecting() const { return connecting; }
	bool isActive() const { return streaming || connecting; }
	void setActive(bool b) { if (b) startStreaming(); else stopStreaming(); }

	QString getSrtUrl() const { return srtUrl; }
	void setSrtUrl(const QString& url);

	QString getSrtMode() const { return srtMode; }
	void setSrtMode(const QString& mode);

	QString getEncoderName() const { return encoderName; }
	void setEncoderName(const QString& name);

	int getBitrate() const { return bitrate; }
	void setBitrate(int kbps);

	int getOutputWidth() const { return outputWidth; }
	int getOutputHeight() const { return outputHeight; }
	void setOutputResolution(int w, int h);

	int getFrameRateCap() const { return frameRateCap; }
	void setFrameRateCap(int fps);

	bool getUseNativeResolution() const { return useNativeResolution; }
	void setUseNativeResolution(bool native);

public slots:
	void startStreaming();
	void stopStreaming();
	void toggleStreaming();
	void showConfigDialog();

signals:
	void streamingStateChanged(bool active);
	void connectingStateChanged(bool connecting);
	void errorMessage(const QString& message);

private:
	void loadSettings();
	void saveSettings();

	// State
	bool streaming = false;
	bool connecting = false;

	// Settings
	QString srtUrl;
	QString srtMode;        // "listener" or "caller"
	QString encoderName;    // "libx264", "h264_nvenc", "h264_vaapi"
	int bitrate = 6000;     // kbps
	int outputWidth = 0;
	int outputHeight = 0;
	bool useNativeResolution = true;
	int frameRateCap = 30;

	// Components
	std::unique_ptr<SrtEncoder> encoder;
	std::unique_ptr<FrameCapture> frameCapture;
	SrtStreamerWindow* configWindow = nullptr;

	// On-screen status (Qt widget layer, not in stream)
	class QLabel* statusLabel = nullptr;
	class QGraphicsProxyWidget* statusProxy = nullptr;
	void updateStatusLabel();

	// Frame timing
	double timeSinceLastFrame = 0.0;
};

// ---- Plugin interface (loader entry point) ----

class SrtStreamerPluginInterface : public QObject, public StelPluginInterface
{
	Q_OBJECT
	Q_PLUGIN_METADATA(IID StelPluginInterface_iid)
	Q_INTERFACES(StelPluginInterface)

public:
	StelModule* getStelModule() const override;
	StelPluginInfo getPluginInfo() const override;
	QObjectList getExtensionList() const override { return QObjectList(); }
};

#endif // SRTSTREAMERMODULE_HPP
