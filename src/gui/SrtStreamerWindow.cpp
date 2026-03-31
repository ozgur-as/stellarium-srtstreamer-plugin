/*
 * Stellarium SRT Streamer Plugin — Configuration dialog
 * Copyright (C) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "SrtStreamerWindow.hpp"
#include "SrtStreamerModule.hpp"
#include "ui_SrtStreamerWindow.h"

#include "StelApp.hpp"
#include "StelLocaleMgr.hpp"
#include "Dialog.hpp"

SrtStreamerWindow::SrtStreamerWindow()
	: StelDialog("SrtStreamer")
{
	ui = new Ui_SrtStreamerWindow();
}

SrtStreamerWindow::~SrtStreamerWindow()
{
	delete ui;
}

void SrtStreamerWindow::setModule(SrtStreamerModule* mod)
{
	module = mod;
}

void SrtStreamerWindow::createDialogContent()
{
	ui->setupUi(dialog);

	connect(&StelApp::getInstance(), SIGNAL(languageChanged()), this, SLOT(retranslate()));
	connect(ui->titleBar, &TitleBar::closeClicked, this, &StelDialog::close);
	connect(ui->titleBar, SIGNAL(movedTo(QPoint)), this, SLOT(handleMovedTo(QPoint)));

	if (!module)
		return;

	// Populate combo boxes
	ui->comboSrtMode->addItem(q_("Listener"), "listener");
	ui->comboSrtMode->addItem(q_("Caller"),   "caller");

	ui->comboEncoder->addItem("libx264 (CPU)",     "libx264");
	ui->comboEncoder->addItem("h264_nvenc (NVIDIA)", "h264_nvenc");
	ui->comboEncoder->addItem("h264_vaapi (Linux)",  "h264_vaapi");

	// Load current settings into UI
	updateUiFromModule();

	// Connect signals
	connect(ui->btnToggle,          &QPushButton::clicked,
	        this, &SrtStreamerWindow::onToggleStreaming);
	connect(module, &SrtStreamerModule::streamingStateChanged,
	        this, &SrtStreamerWindow::onStreamingStateChanged);
	connect(module, &SrtStreamerModule::connectingStateChanged,
	        this, &SrtStreamerWindow::onConnectingStateChanged);
	connect(module, &SrtStreamerModule::errorMessage,
	        this, [this](const QString& msg) {
		ui->labelStatus->setStyleSheet("color: red;");
		ui->labelStatus->setText(msg);
	});

	connect(ui->comboSrtMode,  QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &SrtStreamerWindow::onSrtModeChanged);
	connect(ui->comboEncoder,  QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &SrtStreamerWindow::onEncoderChanged);

	connect(ui->checkNativeRes, &QCheckBox::toggled,
	        this, &SrtStreamerWindow::onNativeResolutionToggled);
	connect(ui->spinBitrate,   QOverload<int>::of(&QSpinBox::valueChanged),
	        this, &SrtStreamerWindow::onBitrateChanged);
	connect(ui->spinFrameRate, QOverload<int>::of(&QSpinBox::valueChanged),
	        this, &SrtStreamerWindow::onFrameRateChanged);
	connect(ui->spinWidth,     QOverload<int>::of(&QSpinBox::valueChanged),
	        this, &SrtStreamerWindow::onResolutionWidthChanged);
	connect(ui->spinHeight,    QOverload<int>::of(&QSpinBox::valueChanged),
	        this, &SrtStreamerWindow::onResolutionHeightChanged);

	connect(ui->editSrtUrl, &QLineEdit::editingFinished,
	        this, &SrtStreamerWindow::onSrtUrlChanged);
}

void SrtStreamerWindow::updateUiFromModule()
{
	if (!module)
		return;

	ui->editSrtUrl->setText(module->getSrtUrl());

	// SRT mode
	int modeIdx = ui->comboSrtMode->findData(module->getSrtMode());
	if (modeIdx >= 0) ui->comboSrtMode->setCurrentIndex(modeIdx);

	// Encoder
	int encIdx = ui->comboEncoder->findData(module->getEncoderName());
	if (encIdx >= 0) ui->comboEncoder->setCurrentIndex(encIdx);

	ui->spinBitrate->setValue(module->getBitrate());
	ui->spinFrameRate->setValue(module->getFrameRateCap());
	ui->checkNativeRes->setChecked(module->getUseNativeResolution());
	ui->spinWidth->setValue(module->getOutputWidth());
	ui->spinHeight->setValue(module->getOutputHeight());
	ui->spinWidth->setEnabled(!module->getUseNativeResolution());
	ui->spinHeight->setEnabled(!module->getUseNativeResolution());

	onStreamingStateChanged(module->isStreaming());
}

void SrtStreamerWindow::onToggleStreaming()
{
	if (!module) return;
	module->toggleStreaming();
}

void SrtStreamerWindow::onStreamingStateChanged(bool active)
{
	if (ui->btnToggle)
	{
		ui->btnToggle->setText(active ? q_("Stop Streaming") : q_("Start Streaming"));
		ui->btnToggle->setChecked(active);
	}

	if (active)
	{
		ui->labelStatus->setStyleSheet("color: green;");
		ui->labelStatus->setText(q_("Streaming"));
	}
	else if (!module->isConnecting())
	{
		// Only reset to "Ready" if there's no error showing
		// (error messages have red style)
		if (!ui->labelStatus->styleSheet().contains("red"))
		{
			ui->labelStatus->setStyleSheet("");
			ui->labelStatus->setText(q_("Ready"));
		}
	}

	// Disable settings while streaming
	bool editable = !active;
	ui->editSrtUrl->setEnabled(editable);
	ui->comboSrtMode->setEnabled(editable);
	ui->comboEncoder->setEnabled(editable);
	ui->spinBitrate->setEnabled(editable);
	ui->spinFrameRate->setEnabled(editable);
	ui->checkNativeRes->setEnabled(editable);
	ui->spinWidth->setEnabled(editable && !module->getUseNativeResolution());
	ui->spinHeight->setEnabled(editable && !module->getUseNativeResolution());
}

void SrtStreamerWindow::onSrtModeChanged(int index)
{
	if (module)
		module->setSrtMode(ui->comboSrtMode->itemData(index).toString());
}

void SrtStreamerWindow::onEncoderChanged(int index)
{
	if (module)
		module->setEncoderName(ui->comboEncoder->itemData(index).toString());
}

void SrtStreamerWindow::onNativeResolutionToggled(bool checked)
{
	if (module)
		module->setUseNativeResolution(checked);

	ui->spinWidth->setEnabled(!checked);
	ui->spinHeight->setEnabled(!checked);
}

void SrtStreamerWindow::onBitrateChanged(int value)
{
	if (module) module->setBitrate(value);
}

void SrtStreamerWindow::onFrameRateChanged(int value)
{
	if (module) module->setFrameRateCap(value);
}

void SrtStreamerWindow::onResolutionWidthChanged(int value)
{
	if (module) module->setOutputResolution(value, module->getOutputHeight());
}

void SrtStreamerWindow::onResolutionHeightChanged(int value)
{
	if (module) module->setOutputResolution(module->getOutputWidth(), value);
}

void SrtStreamerWindow::onSrtUrlChanged()
{
	if (module) module->setSrtUrl(ui->editSrtUrl->text());
}

void SrtStreamerWindow::onConnectingStateChanged(bool connecting)
{
	if (ui->btnToggle)
	{
		ui->btnToggle->setEnabled(true);
		if (connecting)
		{
			ui->btnToggle->setText(q_("Cancel"));
			ui->btnToggle->setChecked(true);
			ui->labelStatus->setStyleSheet("color: orange;");
			ui->labelStatus->setText(q_("Connecting...")); // clears any previous error
		}
		else if (!module->isStreaming())
		{
			ui->btnToggle->setText(q_("Start Streaming"));
			ui->btnToggle->setChecked(false);
		}
	}
}

void SrtStreamerWindow::retranslate()
{
	if (ui)
		ui->retranslateUi(dialog);
}
