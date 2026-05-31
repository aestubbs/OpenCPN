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
#include <QRegularExpression>
#include <QSerialPortInfo>

#include "config_store.h"
#include "model/comm_bridge.h"
#include "model/comm_drv_factory.h"
#include "model/comm_drv_registry.h"
#include "model/comm_navmsg.h"
#include "model/conn_params.h"

namespace {
constexpr char kConfigKey[] = "connections";

// The message bus a connection's driver registers on, by data protocol index.
NavAddr::Bus busForProto(int dataProto) {
  return dataProto == 1 ? NavAddr::Bus::N2000 : NavAddr::Bus::N0183;
}

// dsPortType from the editor's ioSelect index (which already matches the
// enum order: 0 = DS_TYPE_INPUT, 1 = DS_TYPE_INPUT_OUTPUT, 2 = DS_TYPE_OUTPUT).
dsPortType ioSelectFor(int io) {
  switch (io) {
    case 1: return DS_TYPE_INPUT_OUTPUT;
    case 2: return DS_TYPE_OUTPUT;
    default: return DS_TYPE_INPUT;
  }
}

wxArrayString toWxList(const QStringList& l) {
  wxArrayString a;
  for (const QString& s : l) {
    const QString t = s.trimmed();
    if (!t.isEmpty()) a.Add(wxString(t.toUtf8().constData()));
  }
  return a;
}
}  // namespace

namespace ocpn::qtui {

namespace {
QString netProtoText(int p) {
  return p == 1 ? QStringLiteral("UDP") : QStringLiteral("TCP");
}
QString dataProtoText(int p) {
  return p == 1 ? QStringLiteral("NMEA 2000") : QStringLiteral("NMEA 0183");
}
QString ioText(int io) {
  switch (io) {
    case 1: return QStringLiteral("In/Out");
    case 2: return QStringLiteral("Out");
    default: return QStringLiteral("In");
  }
}

// A QML map value can arrive as a real QStringList, a JS array (QVariantList),
// or a single comma/space-separated string typed into the filter field.
QStringList asStringList(const QVariant& v) {
  if (v.typeId() == QMetaType::QStringList) return v.toStringList();
  if (v.typeId() == QMetaType::QVariantList) {
    QStringList r;
    for (const QVariant& e : v.toList()) {
      const QString s = e.toString().trimmed();
      if (!s.isEmpty()) r.append(s);
    }
    return r;
  }
  const QString s = v.toString();
  if (s.trimmed().isEmpty()) return {};
  return s.split(QRegularExpression(QStringLiteral("[,\\s]+")),
                 Qt::SkipEmptyParts);
}

// Build the model ConnectionParams for a connection -- shared by start (apply)
// and stop (disable) so the driver's iface key matches exactly.
ConnectionParams paramsFor(
    int type, int netProto, const QString& address, int port,
    const QString& serialPort, int baud, int dataProto, int ioSelect,
    int inFilterType, const QStringList& inFilter, int outFilterType,
    const QStringList& outFilter, const QString& comment) {
  ConnectionParams params;
  params.Protocol = (dataProto == 1) ? PROTO_NMEA2000 : PROTO_NMEA0183;
  params.IOSelect = ioSelectFor(ioSelect);
  params.InputSentenceListType = (inFilterType == 1) ? BLACKLIST : WHITELIST;
  params.InputSentenceList = toWxList(inFilter);
  params.OutputSentenceListType = (outFilterType == 1) ? BLACKLIST : WHITELIST;
  params.OutputSentenceList = toWxList(outFilter);
  params.UserComment = wxString(comment.toUtf8().constData());
  params.bEnabled = true;

  if (type == 1) {  // serial
    params.Type = SERIAL;
    params.Port = wxString(serialPort.toUtf8().constData());
    params.Baudrate = baud > 0 ? baud : 4800;
  } else {  // network
    params.Type = NETWORK;
    params.NetProtocol = (netProto == 1) ? UDP : TCP;
    params.NetworkAddress = wxString(address.toUtf8().constData());
    params.NetworkPort = port;
  }
  return params;
}
}  // namespace

ConnectionsViewModel::ConnectionsViewModel(QObject* parent) : QObject(parent) {
  load();
}

ConnectionsViewModel::Conn ConnectionsViewModel::fromMap(const QVariantMap& c) {
  Conn x;
  x.type = c.value("type", 0).toInt();
  x.netProto = c.value("netProto", 0).toInt();
  x.address = c.value("address").toString().trimmed();
  x.port = c.value("port", 0).toInt();
  x.serialPort = c.value("serialPort").toString().trimmed();
  x.baud = c.value("baud", 4800).toInt();
  x.dataProto = c.value("dataProto", 0).toInt();
  x.ioSelect = c.value("ioSelect", 0).toInt();
  x.inFilterType = c.value("inFilterType", 0).toInt();
  x.inFilter = asStringList(c.value("inFilter"));
  x.outFilterType = c.value("outFilterType", 0).toInt();
  x.outFilter = asStringList(c.value("outFilter"));
  x.comment = c.value("comment").toString();
  return x;
}

QVariantMap ConnectionsViewModel::toMap(const Conn& c) {
  QVariantMap m;
  m["type"] = c.type;
  m["netProto"] = c.netProto;
  m["netProtoText"] = netProtoText(c.netProto);
  m["address"] = c.address;
  m["port"] = c.port;
  m["serialPort"] = c.serialPort;
  m["baud"] = c.baud;
  m["dataProto"] = c.dataProto;
  m["dataProtoText"] = dataProtoText(c.dataProto);
  m["ioSelect"] = c.ioSelect;
  m["ioText"] = ioText(c.ioSelect);
  m["inFilterType"] = c.inFilterType;
  m["inFilter"] = c.inFilter;
  m["outFilterType"] = c.outFilterType;
  m["outFilter"] = c.outFilter;
  m["comment"] = c.comment;
  m["enabled"] = c.enabled;
  m["summary"] = summaryOf(c);
  return m;
}

QString ConnectionsViewModel::summaryOf(const Conn& c) {
  QString head;
  if (c.type == 1)
    head = QStringLiteral("Serial  %1 @ %2")
               .arg(c.serialPort.isEmpty() ? QStringLiteral("?") : c.serialPort)
               .arg(c.baud);
  else
    head = QStringLiteral("%1  %2:%3")
               .arg(netProtoText(c.netProto), c.address)
               .arg(c.port);
  QString s = QStringLiteral("%1   %2  [%3]")
                  .arg(head, dataProtoText(c.dataProto), ioText(c.ioSelect));
  if (!c.inFilter.isEmpty())
    s += QStringLiteral("  in:%1%2")
             .arg(c.inFilterType == 1 ? QStringLiteral("-") : QString())
             .arg(c.inFilter.join(QLatin1Char(',')));
  if (!c.comment.trimmed().isEmpty())
    s += QStringLiteral("   — %1").arg(c.comment.trimmed());
  return s;
}

bool ConnectionsViewModel::validFor(const Conn& c) const {
  if (c.type == 1) return !c.serialPort.isEmpty();
  return !c.address.isEmpty() && c.port > 0;
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
    c.type = o.value("type").toInt();
    c.netProto = o.value("netProto").toInt();
    c.address = o.value("address").toString();
    c.port = o.value("port").toInt();
    c.serialPort = o.value("serialPort").toString();
    c.baud = o.value("baud").toInt(4800);
    c.dataProto = o.value("dataProto").toInt();
    c.ioSelect = o.value("ioSelect").toInt();
    c.inFilterType = o.value("inFilterType").toInt();
    for (const QJsonValue& f : o.value("inFilter").toArray())
      c.inFilter.append(f.toString());
    c.outFilterType = o.value("outFilterType").toInt();
    for (const QJsonValue& f : o.value("outFilter").toArray())
      c.outFilter.append(f.toString());
    c.comment = o.value("comment").toString();
    c.enabled = o.value("enabled").toBool();
    m_conns.append(c);
  }
}

void ConnectionsViewModel::save() const {
  QJsonArray arr;
  for (const Conn& c : m_conns) {
    QJsonObject o;
    o["type"] = c.type;
    o["netProto"] = c.netProto;
    o["address"] = c.address;
    o["port"] = c.port;
    o["serialPort"] = c.serialPort;
    o["baud"] = c.baud;
    o["dataProto"] = c.dataProto;
    o["ioSelect"] = c.ioSelect;
    o["inFilterType"] = c.inFilterType;
    o["inFilter"] = QJsonArray::fromStringList(c.inFilter);
    o["outFilterType"] = c.outFilterType;
    o["outFilter"] = QJsonArray::fromStringList(c.outFilter);
    o["comment"] = c.comment;
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
  for (const Conn& c : m_conns) out.append(toMap(c));
  return out;
}

QVariantList ConnectionsViewModel::availableSerialPorts() const {
  QVariantList out;
  const auto ports = QSerialPortInfo::availablePorts();
  for (const QSerialPortInfo& p : ports) {
    QVariantMap m;
    m["port"] = p.systemLocation();  // full device path the driver opens
    QString desc = p.portName();
    if (!p.description().isEmpty())
      desc += QStringLiteral(" — %1").arg(p.description());
    if (!p.manufacturer().isEmpty())
      desc += QStringLiteral(" (%1)").arg(p.manufacturer());
    m["description"] = desc;
    out.append(m);
  }
  return out;
}

QVariantList ConnectionsViewModel::baudRates() const {
  return QVariantList{1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
}

QVariantMap ConnectionsViewModel::connectionAt(int index) const {
  if (index < 0 || index >= m_conns.size()) return {};
  return toMap(m_conns[index]);
}

void ConnectionsViewModel::addConnection(const QVariantMap& c) {
  m_conns.append(fromMap(c));
  save();
  Q_EMIT changed();
}

void ConnectionsViewModel::updateConnection(int index, const QVariantMap& c) {
  if (index < 0 || index >= m_conns.size()) return;
  const bool was_enabled = m_conns[index].enabled;
  if (was_enabled) disable(index);  // stop the old driver before re-keying
  Conn n = fromMap(c);
  n.enabled = was_enabled;
  m_conns[index] = n;
  if (was_enabled) apply(index);  // restart with the new settings
  save();
  Q_EMIT changed();
  if (was_enabled) Q_EMIT activated();
}

void ConnectionsViewModel::removeConnection(int index) {
  if (index < 0 || index >= m_conns.size()) return;
  if (m_conns[index].enabled) disable(index);  // stop its driver first
  m_conns.removeAt(index);
  save();
  Q_EMIT changed();
}

void ConnectionsViewModel::setEnabled(int index, bool on) {
  if (index < 0 || index >= m_conns.size()) return;
  m_conns[index].enabled = on;
  if (on)
    apply(index);
  else
    disable(index);  // tear the driver down so the feed stops
  save();
  Q_EMIT changed();
  if (on) Q_EMIT activated();
}

void ConnectionsViewModel::apply(int index) {
  const Conn& c = m_conns[index];
  if (!validFor(c)) return;

  // Make sure the bridge that turns decoded messages into the own-ship
  // globals is alive (AisDecoder is booted in nav_core).
  CommBridge::GetInstance();

  ConnectionParams params =
      paramsFor(c.type, c.netProto, c.address, c.port, c.serialPort, c.baud,
                c.dataProto, c.ioSelect, c.inFilterType, c.inFilter,
                c.outFilterType, c.outFilter, c.comment);
  qInfo("Connection: opening %s", qUtf8Printable(summaryOf(c)));
  MakeCommDriver(&params);  // creates + registers + starts (async, retries)
}

void ConnectionsViewModel::disable(int index) {
  const Conn& c = m_conns[index];
  if (!validFor(c)) return;
  // Match the running driver by its iface key (== GetStrippedDSPort) + bus and
  // deactivate it -- which erases the unique_ptr and tears down the transport
  // so the feed actually stops.
  const ConnectionParams params =
      paramsFor(c.type, c.netProto, c.address, c.port, c.serialPort, c.baud,
                c.dataProto, c.ioSelect, c.inFilterType, c.inFilter,
                c.outFilterType, c.outFilter, c.comment);
  // GetStrippedDSPort() is the comm registry's own iface-key type (std::string)
  // and FindDriver() takes it directly -- a model-API boundary. Hold the key as
  // a QString and convert inline only where the model call demands it.
  const QString iface = QString::fromStdString(params.GetStrippedDSPort());
  auto& reg = CommDriverRegistry::GetInstance();
  DriverPtr& d =
      FindDriver(reg.GetDrivers(), iface.toStdString(), busForProto(c.dataProto));
  if (d) {
    qInfo("Connection: closing %s", qUtf8Printable(iface));
    reg.Deactivate(d);
  }
}

}  // namespace ocpn::qtui
