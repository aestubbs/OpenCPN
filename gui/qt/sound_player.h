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
 * SoundPlayer -- a Qt-native alert-sound player exposed to QML as a singleton.
 * Replaces the wx / portaudio `o_sound` path for the Qt build: it plays a
 * user-chosen sound file (the Options > User Interface > Sounds pickers, and
 * the per-event anchor / AIS / SART / DSC alerts) through QMediaPlayer +
 * QAudioOutput, so any format Qt Multimedia supports works. The full alert
 * *engine* (deciding when to fire each sound) is a separate task; this is the
 * playback primitive it (and the Sounds "Test" buttons) call.
 */

#ifndef OCPN_QT_SOUND_PLAYER_H_
#define OCPN_QT_SOUND_PLAYER_H_

#include <QObject>
#include <QQmlEngine>
#include <QString>

QT_BEGIN_NAMESPACE
class QMediaPlayer;
class QAudioOutput;
QT_END_NAMESPACE

namespace ocpn::qtui {

class SoundPlayer : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)

public:
  explicit SoundPlayer(QObject* parent = nullptr);
  ~SoundPlayer() override;

  bool playing() const;

  /** Play a sound file (absolute path or file:// URL). Empty path is a no-op;
   *  a new call interrupts any sound still playing. */
  Q_INVOKABLE void play(const QString& file);
  /** Stop playback. */
  Q_INVOKABLE void stop();

Q_SIGNALS:
  void playingChanged();

private:
  void ensurePlayer();  // lazily build the QMediaPlayer on first use

  QMediaPlayer* m_player = nullptr;
  QAudioOutput* m_output = nullptr;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_SOUND_PLAYER_H_
