/*
 * Stellarium SRT Streamer Plugin — Configuration dialog
 * Copyright (C) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef SRTSTREAMERWINDOW_HPP
#define SRTSTREAMERWINDOW_HPP

#include "StelDialog.hpp"

class SrtStreamerModule;
class Ui_SrtStreamerWindow;

class SrtStreamerWindow : public StelDialog
{
	Q_OBJECT

public:
	SrtStreamerWindow();
	~SrtStreamerWindow() override;

	void setModule(SrtStreamerModule* mod);

protected:
	void createDialogContent() override;
	void retranslate() override;

private slots:
	void onToggleStreaming();
	void onStreamingStateChanged(bool active);
	void onConnectingStateChanged(bool connecting);
	void onSrtModeChanged(int index);
	void onEncoderChanged(int index);
	void onUse10bitToggled(bool checked);
	void onNativeResolutionToggled(bool checked);
	void onBitrateChanged(int value);
	void onFrameRateChanged(int value);
	void onResolutionWidthChanged(int value);
	void onResolutionHeightChanged(int value);
	void onSrtUrlChanged();

private:
	void updateUiFromModule();
	void updateEncoderComboLabels(bool use10bit);

	Ui_SrtStreamerWindow* ui = nullptr;
	SrtStreamerModule* module = nullptr;
};

#endif // SRTSTREAMERWINDOW_HPP
