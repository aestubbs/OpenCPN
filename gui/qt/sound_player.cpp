/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

/**
 * \file
 *
 * Implement sound_player.h.
 */

#include "sound_player.h"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QUrl>

#include "config_store.h"

namespace ocpn::qtui {

SoundPlayer::SoundPlayer(QObject* parent) : QObject(parent) {
  // Restore the persisted output choice by device id (index 0 = default).
  const QString saved =
      ConfigStore::instance().getString("ui/soundOutputDevice");
  if (!saved.isEmpty()) {
    const auto devs = QMediaDevices::audioOutputs();
    for (int i = 0; i < devs.size(); ++i) {
      if (QString::fromUtf8(devs[i].id()) == saved) {
        m_device_index = i + 1;  // [0] is "System default"
        break;
      }
    }
  }
}

SoundPlayer::~SoundPlayer() = default;

void SoundPlayer::ensurePlayer() {
  if (m_player) return;
  m_player = new QMediaPlayer(this);
  m_output = new QAudioOutput(this);
  m_player->setAudioOutput(m_output);
  applyDevice();
  connect(m_player, &QMediaPlayer::playbackStateChanged, this,
          &SoundPlayer::playingChanged);
}

void SoundPlayer::applyDevice() {
  if (!m_output) return;
  const auto devs = QMediaDevices::audioOutputs();
  if (m_device_index <= 0 || m_device_index > devs.size())
    m_output->setDevice(QMediaDevices::defaultAudioOutput());
  else
    m_output->setDevice(devs[m_device_index - 1]);
}

QStringList SoundPlayer::outputDevices() const {
  QStringList out{tr("System default")};
  for (const QAudioDevice& d : QMediaDevices::audioOutputs())
    out.append(d.description());
  return out;
}

void SoundPlayer::setOutputDevice(int index) {
  if (index == m_device_index || index < 0) return;
  m_device_index = index;
  const auto devs = QMediaDevices::audioOutputs();
  ConfigStore::instance().setString(
      "ui/soundOutputDevice",
      (index > 0 && index <= devs.size())
          ? QString::fromUtf8(devs[index - 1].id())
          : QString());
  applyDevice();
  emit outputDeviceChanged();
}

bool SoundPlayer::playing() const {
  return m_player &&
         m_player->playbackState() == QMediaPlayer::PlayingState;
}

void SoundPlayer::play(const QString& file) {
  const QString f = file.trimmed();
  if (f.isEmpty()) return;
  ensurePlayer();
  const QUrl url = f.startsWith(QStringLiteral("file:"))
                       ? QUrl(f)
                       : QUrl::fromLocalFile(f);
  m_player->stop();
  m_player->setSource(url);
  m_player->play();
}

void SoundPlayer::stop() {
  if (m_player) m_player->stop();
}

}  // namespace ocpn::qtui
