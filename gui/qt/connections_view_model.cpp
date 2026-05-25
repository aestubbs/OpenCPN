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
 * Implement connections_view_model.h.
 */

#include "connections_view_model.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include "config_store.h"
#include "model/comm_bridge.h"
#include "model/comm_drv_factory.h"
#include "model/conn_params.h"

namespace {
constexpr char kConfigKey[] = "connections";
}

namespace ocpn::qtui {

namespace {
QString netProtoText(int p) { return p == 1 ? QStringLiteral("UDP")
                                            : QStringLiteral("TCP"); }
QString dataProtoText(int p) {
  switch (p) {
    case 1: return QStringLiteral("NMEA 2000");
    case 2: return QStringLiteral("SignalK");
    default: return QStringLiteral("NMEA 0183");
  }
}
}  // namespace

ConnectionsViewModel::ConnectionsViewModel(QObject* parent) : QObject(parent) {
  load();
}

void ConnectionsViewModel::load() {
  const QString json =
      ConfigStore::instance().getString(QString::fromLatin1(kConfigKey));
  if (json.isEmpty()) return;
  const QJsonArray arr = QJsonDocument::fromJson(json.toUtf8()).array();
  m_conns.clear();
  for (const QJsonValue& v : arr) {
    const QJsonObject o = v.toObject();
    Conn c;
    c.netProto = o.value("netProto").toInt();
    c.address = o.value("address").toString();
    c.port = o.value("port").toInt();
    c.dataProto = o.value("dataProto").toInt();
    c.enabled = o.value("enabled").toBool();
    m_conns.append(c);
  }
}

void ConnectionsViewModel::save() const {
  QJsonArray arr;
  for (const Conn& c : m_conns) {
    QJsonObject o;
    o["netProto"] = c.netProto;
    o["address"] = c.address;
    o["port"] = c.port;
    o["dataProto"] = c.dataProto;
    o["enabled"] = c.enabled;
    arr.append(o);
  }
  ConfigStore::instance().setString(
      QString::fromLatin1(kConfigKey),
      QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void ConnectionsViewModel::activatePersisted() {
  bool any = false;
  for (int i = 0; i < m_conns.size(); ++i)
    if (m_conns[i].enabled) {
      apply(i);
      any = true;
    }
  if (any) Q_EMIT activated();
}

QVariantList ConnectionsViewModel::connections() const {
  QVariantList out;
  for (const Conn& c : m_conns) {
    QVariantMap m;
    m["netProto"] = c.netProto;
    m["netProtoText"] = netProtoText(c.netProto);
    m["address"] = c.address;
    m["port"] = c.port;
    m["dataProto"] = c.dataProto;
    m["dataProtoText"] = dataProtoText(c.dataProto);
    m["enabled"] = c.enabled;
    m["summary"] = QStringLiteral("%1  %2:%3  (%4)")
                       .arg(netProtoText(c.netProto), c.address)
                       .arg(c.port)
                       .arg(dataProtoText(c.dataProto));
    out.append(m);
  }
  return out;
}

void ConnectionsViewModel::addConnection(int netProto, const QString& address,
                                         int port, int dataProto) {
  Conn c;
  c.netProto = netProto;
  c.address = address.trimmed();
  c.port = port;
  c.dataProto = dataProto;
  c.enabled = false;
  m_conns.append(c);
  save();
  Q_EMIT changed();
}

void ConnectionsViewModel::removeConnection(int index) {
  if (index < 0 || index >= m_conns.size()) return;
  m_conns.removeAt(index);
  save();
  Q_EMIT changed();
}

void ConnectionsViewModel::setEnabled(int index, bool on) {
  if (index < 0 || index >= m_conns.size()) return;
  m_conns[index].enabled = on;
  if (on) apply(index);
  save();
  Q_EMIT changed();
  if (on) Q_EMIT activated();
}

void ConnectionsViewModel::apply(int index) {
  const Conn& c = m_conns[index];
  if (c.address.isEmpty() || c.port <= 0) return;

  // Make sure the bridge that turns decoded messages into the own-ship
  // globals is alive (AisDecoder is booted in nav_core).
  CommBridge::GetInstance();

  ConnectionParams params;
  params.Type = NETWORK;
  params.NetProtocol = (c.netProto == 1) ? UDP : TCP;
  params.NetworkAddress = wxString(c.address.toUtf8().constData());
  params.NetworkPort = c.port;
  switch (c.dataProto) {
    case 1: params.Protocol = PROTO_NMEA2000; break;
    case 2: params.Protocol = PROTO_SIGNALK; break;
    default: params.Protocol = PROTO_NMEA0183; break;
  }
  params.IOSelect = DS_TYPE_INPUT;
  params.bEnabled = true;
  qInfo("Connection: opening %s %s:%d proto=%d", c.netProto == 1 ? "UDP" : "TCP",
        c.address.toUtf8().constData(), c.port, c.dataProto);
  MakeCommDriver(&params);  // creates + registers + starts (async, retries)
}

}  // namespace ocpn::qtui
