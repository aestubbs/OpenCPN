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
  // Output device: index into outputDevices() (0 = system default).
  // Persisted by device id so a re-plug keeps the choice.
  Q_PROPERTY(int outputDevice READ outputDevice WRITE setOutputDevice NOTIFY
                 outputDeviceChanged)

public:
  ~SoundPlayer() override;

  bool playing() const;

  /** Play a sound file (absolute path or file:// URL). Empty path is a no-op;
   *  a new call interrupts any sound still playing. */
  Q_INVOKABLE void play(const QString& file);
  /** Available audio outputs: display names; [0] is "System default". */
  Q_INVOKABLE QStringList outputDevices() const;
  int outputDevice() const { return m_device_index; }
  void setOutputDevice(int index);
  /** Stop playback. */
  Q_INVOKABLE void stop();

Q_SIGNALS:
  void outputDeviceChanged();
  void playingChanged();

private:
  // Constructor is PRIVATE so the QML engine cannot default-
  // construct a second instance: Qt picks the Constructor mode
  // over the create() factory whenever the type is default-
  // constructible (qqmlprivate.h singletonConstructionMode),
  // which split every singleton into a C++ brain and a QML
  // brain. Private ctor => Factory mode => create() => the
  // ONE shared instance().
  explicit SoundPlayer(QObject* parent = nullptr);

  void ensurePlayer();  // lazily build the QMediaPlayer on first use
  void applyDevice();   // point m_output at the chosen audio device

  QMediaPlayer* m_player = nullptr;
  QAudioOutput* m_output = nullptr;
  int m_device_index = 0;  // 0 = system default
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_SOUND_PLAYER_H_
