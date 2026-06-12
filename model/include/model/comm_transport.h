/***************************************************************************
 *   Copyright (C) 2026 OpenCPN Developers                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 **************************************************************************/

/**
 * \file
 *
 * Comms framework -- the media adaptor layer.
 *
 * A CommTransport wraps exactly one Qt I/O class and exposes a uniform,
 * protocol-agnostic byte pipe: Open/Close/Write plus DataReceived /
 * Connected / Disconnected / ErrorOccurred signals. Nothing in here is
 * protocol-aware -- framing is a Framer's job, decoding a ProtocolDecoder's,
 * and reconnect/watchdog/stats the generic CommDriver's. See
 * docs/QT_MIGRATION_COMMS_ARCH.md (task P1.5i).
 *
 * The build defines QT_NO_KEYWORDS (task P1.5a) so Q_SIGNALS / Q_SLOTS /
 * Q_EMIT are used instead of the signals/slots/emit macros.
 */

#ifndef COMM_TRANSPORT_H
#define COMM_TRANSPORT_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

class QSerialPort;
class QTcpSocket;
class QTimer;
class QUdpSocket;
class QWebSocket;

/**
 * Abstract media adaptor.
 *
 * One subclass per Qt I/O class. A transport carries raw bytes only; for a
 * frame-native medium (CAN, WebSocket) each DataReceived chunk happens to be
 * exactly one frame, which the PassThroughFramer relies on.
 */
class CommTransport : public QObject {
  Q_OBJECT

public:
  explicit CommTransport(QObject* parent = nullptr) : QObject(parent) {}
  ~CommTransport() override = default;

  /**
   * Open the underlying device. A false return means the device could not
   * be opened. For sockets that connect asynchronously a true return only
   * means the attempt was started -- wait for the Connected() signal.
   */
  virtual bool Open() = 0;

  /** Close the device. Idempotent. */
  virtual void Close() = 0;

  /** True while the device is open and usable for I/O. */
  virtual bool IsOpen() const = 0;

  /**
   * Send raw bytes. Returns false if nothing could be queued (device not
   * open, write error).
   */
  virtual bool Write(const QByteArray& data) = 0;

Q_SIGNALS:
  /** A chunk of raw bytes arrived from the device. */
  void DataReceived(const QByteArray& data);

  /** The device became ready for I/O (socket connected, port opened). */
  void Connected();

  /** The device is no longer usable (peer closed, port lost). */
  void Disconnected();

  /** A recoverable or fatal device error; message is human-readable. */
  void ErrorOccurred(const QString& message);
};

/**
 * Serial-port transport (QSerialPort). Configured 8-N-1 with no flow
 * control -- the universal NMEA wiring; only the baud rate varies.
 */
class SerialTransport : public CommTransport {
  Q_OBJECT

public:
  SerialTransport(const QString& port_name, qint32 baud_rate,
                  QObject* parent = nullptr);
  ~SerialTransport() override;

  bool Open() override;
  void Close() override;
  bool IsOpen() const override;
  bool Write(const QByteArray& data) override;

private Q_SLOTS:
  void OnReadyRead();
  void OnError();

private:
  const QString m_port_name;
  const qint32 m_baud_rate;
  QSerialPort* m_port;  ///< owned via QObject parenting to this transport
};

/**
 * TCP client transport (QTcpSocket). Connects asynchronously -- Open()
 * starts the attempt and Connected() fires when it succeeds.
 */
class TcpClientTransport : public CommTransport {
  Q_OBJECT

public:
  TcpClientTransport(const QString& host, quint16 port,
                     QObject* parent = nullptr);
  ~TcpClientTransport() override;

  bool Open() override;
  void Close() override;
  bool IsOpen() const override;
  bool Write(const QByteArray& data) override;

private Q_SLOTS:
  void OnReadyRead();
  void OnConnected();
  void OnDisconnected();
  void OnError();

private:
  const QString m_host;
  const quint16 m_port;
  QTcpSocket* m_socket;  ///< owned via QObject parenting to this transport
};

/**
 * UDP transport (QUdpSocket). Receives on the bound port and transmits to
 * host:port. With multicast=true the bound socket joins the multicast group
 * given by host; otherwise host is a unicast/broadcast TX peer.
 *
 * UDP is connectionless; for a uniform driver lifecycle Connected() is
 * emitted as soon as the socket binds successfully.
 */
class UdpTransport : public CommTransport {
  Q_OBJECT

public:
  UdpTransport(const QString& host, quint16 port, bool multicast = false,
               QObject* parent = nullptr);
  ~UdpTransport() override;

  bool Open() override;
  void Close() override;
  bool IsOpen() const override;
  bool Write(const QByteArray& data) override;

private Q_SLOTS:
  void OnReadyRead();
  void OnError();

private:
  const QString m_host;
  const quint16 m_port;
  const bool m_multicast;
  QUdpSocket* m_socket;  ///< owned via QObject parenting to this transport
};

/**
 * WebSocket transport (QWebSocket). Frame-native: every received text or
 * binary message is exactly one DataReceived chunk -- pair it with the
 * PassThroughFramer.
 *
 * An optional alternate URL is switched to on error, flip-flopping between
 * the two on every failure -- the wss:// <-> ws:// dance SignalK servers
 * need (the caller does not know which scheme a server speaks). TLS
 * certificate errors are ignored: boat-LAN servers run self-signed certs,
 * matching the legacy driver's disabled validation. A 30 s ping keeps
 * NAT / proxy paths alive.
 */
class WebSocketTransport : public CommTransport {
  Q_OBJECT

public:
  WebSocketTransport(const QUrl& url, const QUrl& alternate_url = QUrl(),
                     QObject* parent = nullptr);
  ~WebSocketTransport() override;

  bool Open() override;
  void Close() override;
  bool IsOpen() const override;

  /** Sends data as one websocket TEXT message (the JSON protocols). */
  bool Write(const QByteArray& data) override;

private Q_SLOTS:
  void OnConnected();
  void OnDisconnected();
  void OnTextMessage(const QString& message);
  void OnBinaryMessage(const QByteArray& message);
  void OnError();

private:
  const QUrl m_url;
  const QUrl m_alternate;
  bool m_use_alternate = false;
  QWebSocket* m_ws;      ///< owned via QObject parenting to this transport
  QTimer* m_ping_timer;  ///< 30 s keepalive while connected
};

#endif  // COMM_TRANSPORT_H
