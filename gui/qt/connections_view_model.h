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
 * ConnectionsViewModel -- the data-source list behind the Options dialog's
 * Connections tab (#34). Holds user-defined connections and, when one is
 * enabled, builds a model ConnectionParams and hands it to MakeCommDriver so
 * the comm framework opens the transport and feeds the model (NavMsgBus ->
 * CommBridge + AisDecoder). Enabling a connection emits activated() so the
 * canvas switches to live mode and mirrors the model.
 *
 * Parity scope (P3 Connections editor, wx `ConnectionParams`): the editor
 * covers every transport/protocol the wx-free comm framework actually wires --
 * **Serial** (NMEA 0183 / NMEA 2000) and **Network** TCP/UDP (NMEA 0183 /
 * NMEA 2000) -- plus I/O direction, input/output sentence filters and a user
 * comment. GPSD / SignalK / SocketCAN / TCP-server are deliberately *not*
 * offered: `MakeCommDriver` parks them (no driver), so exposing them would be
 * inert. They return when the framework grows the transports.
 *
 * The model-touching code lives in the .cpp so model/wx headers stay out of
 * this header.
 */

#ifndef OCPN_QT_CONNECTIONS_VIEW_MODEL_H_
#define OCPN_QT_CONNECTIONS_VIEW_MODEL_H_

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace ocpn::qtui {

class ConnectionsViewModel : public QObject {
  Q_OBJECT
  // Each entry is a QVariantMap (see toMap) consumed by the QML list + editor.
  Q_PROPERTY(QVariantList connections READ connections NOTIFY changed)

public:
  explicit ConnectionsViewModel(QObject* parent = nullptr);

  QVariantList connections() const;

  /**
   * Add / replace a connection from a QML property map. Recognised keys (all
   * optional, defaulted): `type` (0 network, 1 serial), `netProto` (0 TCP,
   * 1 UDP), `address`, `port`, `serialPort`, `baud`, `dataProto` (0 NMEA0183,
   * 1 NMEA2000), `ioSelect` (dsPortType: 0 input, 1 both, 2 output),
   * `inFilterType`/`outFilterType` (0 whitelist, 1 blacklist),
   * `inFilter`/`outFilter` (QStringList or comma/space-separated string),
   * `comment`.
   */
  Q_INVOKABLE void addConnection(const QVariantMap& c);
  Q_INVOKABLE void updateConnection(int index, const QVariantMap& c);
  /** The stored fields for `index`, for populating the edit form. */
  Q_INVOKABLE QVariantMap connectionAt(int index) const;
  Q_INVOKABLE void removeConnection(int index);
  Q_INVOKABLE void setEnabled(int index, bool on);

  /** Serial ports present on this host (QSerialPortInfo) as
   *  `{ port, description }` maps for the editor's port dropdown. */
  Q_INVOKABLE QVariantList availableSerialPorts() const;
  /** Common serial baud rates for the editor. */
  Q_INVOKABLE QVariantList baudRates() const;

  /** Re-open any persisted connections that were enabled (auto-reconnect on
   *  launch) and emit activated() if any. Call once after wiring activated(). */
  void activatePersisted();

Q_SIGNALS:
  void changed();
  /** A connection was enabled -- the canvas should go live + mirror the
   *  model so the incoming data shows. */
  void activated();

private:
  struct Conn {
    int type = 0;       // 0 network, 1 serial
    int netProto = 0;   // 0 TCP, 1 UDP
    QString address;
    int port = 0;
    QString serialPort;
    int baud = 4800;
    int dataProto = 0;  // 0 NMEA0183, 1 NMEA2000
    int ioSelect = 0;   // dsPortType: 0 input, 1 both, 2 output
    int inFilterType = 0;   // 0 whitelist, 1 blacklist
    QStringList inFilter;
    int outFilterType = 0;
    QStringList outFilter;
    QString comment;
    bool enabled = false;
  };

  static Conn fromMap(const QVariantMap& c);
  static QVariantMap toMap(const Conn& c);
  static QString summaryOf(const Conn& c);
  bool validFor(const Conn& c) const;  // has enough to open a driver

  void apply(int index);    // build ConnectionParams + MakeCommDriver
  void disable(int index);  // find the running driver + Deactivate (stop) it
  void load();              // read the persisted list from the config store
  void save() const;        // write the list to the config store

  QVector<Conn> m_conns;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CONNECTIONS_VIEW_MODEL_H_
