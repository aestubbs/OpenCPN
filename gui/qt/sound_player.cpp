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
#include <QUrl>

namespace ocpn::qtui {

SoundPlayer::SoundPlayer(QObject* parent) : QObject(parent) {}

SoundPlayer::~SoundPlayer() = default;

void SoundPlayer::ensurePlayer() {
  if (m_player) return;
  m_player = new QMediaPlayer(this);
  m_output = new QAudioOutput(this);
  m_player->setAudioOutput(m_output);
  connect(m_player, &QMediaPlayer::playbackStateChanged, this,
          &SoundPlayer::playingChanged);
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
