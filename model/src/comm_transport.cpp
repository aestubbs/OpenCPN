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
 * Implement comm_transport.h -- the Qt-native media adaptors.
 */

#include <QHostAddress>
#include <QNetworkDatagram>
#include <QSerialPort>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QWebSocket>

#include "model/comm_transport.h"

// ---------------------------------------------------------------------------
// SerialTransport
// ---------------------------------------------------------------------------

SerialTransport::SerialTransport(const QString& port_name, qint32 baud_rate,
                                 QObject* parent)
    : CommTransport(parent),
      m_port_name(port_name),
      m_baud_rate(baud_rate),
      m_port(nullptr) {}

SerialTransport::~SerialTransport() { SerialTransport::Close(); }

bool SerialTransport::Open() {
  if (!m_port) {
    m_port = new QSerialPort(this);
    connect(m_port, &QSerialPort::readyRead, this,
            &SerialTransport::OnReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this,
            &SerialTransport::OnError);
  }
  m_port->setPortName(m_port_name);
  m_port->setBaudRate(m_baud_rate);
  m_port->setDataBits(QSerialPort::Data8);
  m_port->setParity(QSerialPort::NoParity);
  m_port->setStopBits(QSerialPort::OneStop);
  m_port->setFlowControl(QSerialPort::NoFlowControl);

  if (!m_port->open(QIODevice::ReadWrite)) {
    Q_EMIT ErrorOccurred(m_port->errorString());
    return false;
  }
  Q_EMIT Connected();
  return true;
}

void SerialTransport::Close() {
  if (m_port && m_port->isOpen()) m_port->close();
}

bool SerialTransport::IsOpen() const { return m_port && m_port->isOpen(); }

bool SerialTransport::Write(const QByteArray& data) {
  if (!IsOpen()) return false;
  if (m_port->write(data) < 0) return false;
  m_port->flush();
  return true;
}

void SerialTransport::OnReadyRead() {
  const QByteArray chunk = m_port->readAll();
  if (!chunk.isEmpty()) Q_EMIT DataReceived(chunk);
}

void SerialTransport::OnError() {
  const QSerialPort::SerialPortError err = m_port->error();
  if (err == QSerialPort::NoError) return;

  Q_EMIT ErrorOccurred(m_port->errorString());

  // A device that disappears (USB adaptor unplugged, port lost) -- close it
  // so the owning CommDriver's reconnect timer can retry from a clean state.
  if (err == QSerialPort::ResourceError ||
      err == QSerialPort::PermissionError ||
      err == QSerialPort::DeviceNotFoundError ||
      err == QSerialPort::OpenError) {
    if (m_port->isOpen()) m_port->close();
    Q_EMIT Disconnected();
  }
  m_port->clearError();
}

// ---------------------------------------------------------------------------
// TcpClientTransport
// ---------------------------------------------------------------------------

TcpClientTransport::TcpClientTransport(const QString& host, quint16 port,
                                       QObject* parent,
                                       const QByteArray& connect_greeting)
    : CommTransport(parent),
      m_host(host),
      m_port(port),
      m_greeting(connect_greeting),
      m_socket(nullptr) {}

TcpClientTransport::~TcpClientTransport() { TcpClientTransport::Close(); }

bool TcpClientTransport::Open() {
  if (!m_socket) {
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::readyRead, this,
            &TcpClientTransport::OnReadyRead);
    connect(m_socket, &QTcpSocket::connected, this,
            &TcpClientTransport::OnConnected);
    connect(m_socket, &QTcpSocket::disconnected, this,
            &TcpClientTransport::OnDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this,
            &TcpClientTransport::OnError);
  }
  // Connects asynchronously; Connected() is emitted from OnConnected().
  m_socket->connectToHost(m_host, m_port);
  return true;
}

void TcpClientTransport::Close() {
  if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState)
    m_socket->abort();
}

bool TcpClientTransport::IsOpen() const {
  return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

bool TcpClientTransport::Write(const QByteArray& data) {
  if (!IsOpen()) return false;
  return m_socket->write(data) >= 0;
}

void TcpClientTransport::OnReadyRead() {
  const QByteArray chunk = m_socket->readAll();
  if (!chunk.isEmpty()) Q_EMIT DataReceived(chunk);
}

void TcpClientTransport::OnConnected() {
  // The greeting (e.g. gpsd's ?WATCH subscription) goes out on every
  // (re)connect, before consumers learn the link is up.
  if (!m_greeting.isEmpty()) m_socket->write(m_greeting);
  Q_EMIT Connected();
}

void TcpClientTransport::OnDisconnected() { Q_EMIT Disconnected(); }

void TcpClientTransport::OnError() {
  Q_EMIT ErrorOccurred(m_socket->errorString());
}

// ---------------------------------------------------------------------------
// TcpServerTransport
// ---------------------------------------------------------------------------

TcpServerTransport::TcpServerTransport(quint16 port, QObject* parent)
    : CommTransport(parent), m_port(port), m_server(nullptr) {}

TcpServerTransport::~TcpServerTransport() { TcpServerTransport::Close(); }

bool TcpServerTransport::Open() {
  if (!m_server) {
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this,
            &TcpServerTransport::OnNewConnection);
  }
  if (m_server->isListening()) return true;
  if (!m_server->listen(QHostAddress::Any, m_port)) {
    Q_EMIT ErrorOccurred(m_server->errorString());
    return false;
  }
  // Like UdpTransport's bind: the transport is usable once listening --
  // clients come and go without changing the driver's lifecycle.
  Q_EMIT Connected();
  return true;
}

void TcpServerTransport::Close() {
  if (!m_server) return;
  for (QTcpSocket* c : m_clients) c->abort();
  m_clients.clear();
  if (m_server->isListening()) m_server->close();
}

bool TcpServerTransport::IsOpen() const {
  return m_server && m_server->isListening();
}

bool TcpServerTransport::Write(const QByteArray& data) {
  bool any = false;
  for (QTcpSocket* c : m_clients)
    if (c->state() == QAbstractSocket::ConnectedState)
      any = c->write(data) >= 0 || any;
  return any;
}

void TcpServerTransport::OnNewConnection() {
  while (QTcpSocket* client = m_server->nextPendingConnection()) {
    m_clients.append(client);
    connect(client, &QTcpSocket::readyRead, this, [this, client] {
      const QByteArray chunk = client->readAll();
      if (!chunk.isEmpty()) Q_EMIT DataReceived(chunk);
    });
    connect(client, &QTcpSocket::disconnected, this, [this, client] {
      m_clients.removeAll(client);
      client->deleteLater();
    });
  }
}

// ---------------------------------------------------------------------------
// UdpTransport
// ---------------------------------------------------------------------------

UdpTransport::UdpTransport(const QString& host, quint16 port, bool multicast,
                           QObject* parent)
    : CommTransport(parent),
      m_host(host),
      m_port(port),
      m_multicast(multicast),
      m_socket(nullptr) {}

UdpTransport::~UdpTransport() { UdpTransport::Close(); }

bool UdpTransport::Open() {
  if (!m_socket) {
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this,
            &UdpTransport::OnReadyRead);
    connect(m_socket, &QUdpSocket::errorOccurred, this,
            &UdpTransport::OnError);
  }
  // Bind shareable: multicast receivers must share the port with other
  // listeners, and OpenCPN itself may run several connections (or a separate
  // RX and TX direction) on one port -- the legacy driver bound REUSEADDR.
  const QUdpSocket::BindMode mode =
      QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
  if (!m_socket->bind(QHostAddress::AnyIPv4, m_port, mode)) {
    Q_EMIT ErrorOccurred(m_socket->errorString());
    return false;
  }
  if (m_multicast && !m_socket->joinMulticastGroup(QHostAddress(m_host))) {
    Q_EMIT ErrorOccurred(m_socket->errorString());
    return false;
  }
  Q_EMIT Connected();
  return true;
}

void UdpTransport::Close() {
  if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState)
    m_socket->close();
}

bool UdpTransport::IsOpen() const {
  return m_socket && m_socket->state() == QAbstractSocket::BoundState;
}

bool UdpTransport::Write(const QByteArray& data) {
  if (!m_socket) return false;
  return m_socket->writeDatagram(data, QHostAddress(m_host), m_port) ==
         data.size();
}

void UdpTransport::OnReadyRead() {
  while (m_socket->hasPendingDatagrams()) {
    const QNetworkDatagram dg = m_socket->receiveDatagram();
    if (!dg.data().isEmpty()) Q_EMIT DataReceived(dg.data());
  }
}

void UdpTransport::OnError() {
  Q_EMIT ErrorOccurred(m_socket->errorString());
}

// ---------------------------------------------------------------------------
// WebSocketTransport
// ---------------------------------------------------------------------------

WebSocketTransport::WebSocketTransport(const QUrl& url,
                                       const QUrl& alternate_url,
                                       QObject* parent)
    : CommTransport(parent),
      m_url(url),
      m_alternate(alternate_url),
      m_ws(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this)),
      m_ping_timer(new QTimer(this)) {
  connect(m_ws, &QWebSocket::connected, this,
          &WebSocketTransport::OnConnected);
  connect(m_ws, &QWebSocket::disconnected, this,
          &WebSocketTransport::OnDisconnected);
  connect(m_ws, &QWebSocket::textMessageReceived, this,
          &WebSocketTransport::OnTextMessage);
  connect(m_ws, &QWebSocket::binaryMessageReceived, this,
          &WebSocketTransport::OnBinaryMessage);
  connect(m_ws, &QWebSocket::errorOccurred, this,
          &WebSocketTransport::OnError);
#ifndef QT_NO_SSL
  // Boat-LAN servers run self-signed certificates; the legacy driver
  // disabled certificate validation, so this transport does too.
  connect(m_ws, &QWebSocket::sslErrors, this,
          [this](const QList<QSslError>&) { m_ws->ignoreSslErrors(); });
#endif
  m_ping_timer->setInterval(30 * 1000);
  connect(m_ping_timer, &QTimer::timeout, this,
          [this] { m_ws->ping(); });
}

WebSocketTransport::~WebSocketTransport() { WebSocketTransport::Close(); }

bool WebSocketTransport::Open() {
  const QUrl& url =
      m_use_alternate && m_alternate.isValid() ? m_alternate : m_url;
  // Connects asynchronously; Connected() is emitted from OnConnected().
  m_ws->open(url);
  return true;
}

void WebSocketTransport::Close() {
  m_ping_timer->stop();
  if (m_ws->state() != QAbstractSocket::UnconnectedState) m_ws->close();
}

bool WebSocketTransport::IsOpen() const {
  return m_ws->state() == QAbstractSocket::ConnectedState;
}

bool WebSocketTransport::Write(const QByteArray& data) {
  if (!IsOpen()) return false;
  return m_ws->sendTextMessage(QString::fromUtf8(data)) > 0;
}

void WebSocketTransport::OnConnected() {
  m_ping_timer->start();
  Q_EMIT Connected();
}

void WebSocketTransport::OnDisconnected() {
  m_ping_timer->stop();
  Q_EMIT Disconnected();
}

void WebSocketTransport::OnTextMessage(const QString& message) {
  Q_EMIT DataReceived(message.toUtf8());
}

void WebSocketTransport::OnBinaryMessage(const QByteArray& message) {
  if (!message.isEmpty()) Q_EMIT DataReceived(message);
}

void WebSocketTransport::OnError() {
  // Flip to the other URL for the next attempt (the wss <-> ws dance).
  if (m_alternate.isValid()) m_use_alternate = !m_use_alternate;
  Q_EMIT ErrorOccurred(m_ws->errorString());
  // A failed CONNECT attempt never emits disconnected(), so the generic
  // driver's reconnect timer would not be armed -- emit it here when the
  // socket is not connected. OnDisconnected handles the connected case.
  if (m_ws->state() == QAbstractSocket::UnconnectedState)
    Q_EMIT Disconnected();
}
