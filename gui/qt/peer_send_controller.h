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
 * PeerSendController -- the Qt front end for "Send to Peer" (P3.18 tier 4):
 * mDNS discovery of other OpenCPN instances (model FindAllOCPNServers ->
 * MdnsCache) and the route/mark/track transfer (model SendNavobjects,
 * QNAM-based since P1.13). The transfer runs on the MAIN thread -- the
 * model call blocks via an internal QEventLoop, and its synchronous
 * PIN-confirm callback is satisfied by emitting pinRequested() and
 * spinning a local QEventLoop until QML answers via providePin()/
 * cancelPin() (the wx dialog did the same synchronously).
 */

#ifndef OCPN_QT_PEER_SEND_CONTROLLER_H_
#define OCPN_QT_PEER_SEND_CONTROLLER_H_

#include <QObject>
#include <QString>
#include <QVariantList>

#include "observable_evtvar.h"  // EventVar (transfer progress)

QT_BEGIN_NAMESPACE
class QEventLoop;
QT_END_NAMESPACE

namespace ocpn::qtui {

class PeerSendController : public QObject {
  Q_OBJECT
  // Discovered OpenCPN peers: {name, ip, port} maps (mDNS scan results).
  Q_PROPERTY(QVariantList peers READ peers NOTIFY peersChanged)
  Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
  Q_PROPERTY(bool sending READ sending NOTIFY sendingChanged)
  // Human-readable outcome of the last transfer ("" = none yet).
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
  explicit PeerSendController(QObject* parent = nullptr);

  QVariantList peers() const { return m_peers; }
  bool scanning() const { return m_scanning; }
  bool sending() const { return m_sending; }
  QString status() const { return m_status; }

  /** Re-run the mDNS scan (a few seconds, on a worker thread). */
  Q_INVOKABLE void scan();

  /** Send one object to the peer at `ip` (host[:port]). Blocking (main
   *  thread, event-loop pumped); returns true on transfer success. */
  Q_INVOKABLE bool sendRoute(int routeIndex, const QString& ip, bool activate);
  Q_INVOKABLE bool sendMark(const QString& guid, const QString& ip);
  Q_INVOKABLE bool sendTrack(const QString& guid, const QString& ip);

  /** Answer an outstanding pinRequested() (the server asked for its PIN). */
  Q_INVOKABLE void providePin(const QString& pin);
  Q_INVOKABLE void cancelPin();

signals:
  void peersChanged();
  void scanningChanged();
  void sendingChanged();
  void statusChanged();
  /** The destination wants its pairing PIN typed in: open the PIN dialog
   *  and answer with providePin()/cancelPin(). */
  void pinRequested();

private:
  bool sendObjects(const QString& ip, bool activate, void* route, void* mark,
                   void* track);
  void refreshPeers();

  QVariantList m_peers;
  bool m_scanning = false;
  bool m_sending = false;
  QString m_status;

  EventVar m_progress;        // model progress notifications (unused v1)
  QEventLoop* m_pin_loop = nullptr;
  QString m_pin;
  bool m_pin_ok = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_PEER_SEND_CONTROLLER_H_
