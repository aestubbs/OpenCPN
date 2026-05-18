/***************************************************************************
 *   Copyright (C) 2023 by David Register                                  *
 *   Copyright (C) 2023 Alec Leamas                                        *
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
 * Nmea2000 IP network driver.
 *
 * Qt-native rewrite (Qt/QtQuick migration, task P1.5a) -- the driver is a
 * QObject using QTcpSocket / QTcpServer / QUdpSocket and QTimer. Decoded
 * payloads are delivered through a queued signal/slot, see
 * docs/QT_MIGRATION_N2K_NET_PLAN.md.
 */

#ifndef _COMMDRIVERN2KNET_H
#define _COMMDRIVERN2KNET_H

#include <memory>
#include <string>
#include <vector>

#include <QObject>
#include <QDateTime>

#include "model/comm_buffers.h"
#include "model/comm_can_util.h"
#include "model/comm_drv_n2k.h"
#include "model/comm_drv_stats.h"
#include "model/conn_params.h"

class QTcpSocket;
class QTcpServer;
class QUdpSocket;
class QTimer;

#define RX_BUFFER_SIZE_NET 4096

#define ESCAPE 0x10
#define STARTOFTEXT 0x02
#define ENDOFTEXT 0x03

#define MsgTypeN2kData 0x93
#define MsgTypeN2kRequest 0x94

typedef enum {
  N2KFormat_Undefined = 0,
  N2KFormat_YD_RAW,
  N2KFormat_Actisense_RAW_ASCII,
  N2KFormat_Actisense_N2K_ASCII,
  N2KFormat_Actisense_N2K,
  N2KFormat_Actisense_RAW,
  N2KFormat_Actisense_NGT,
  N2KFormat_SeaSmart,
  N2KFormat_MiniPlex
} N2K_Format;

typedef enum { TX_FORMAT_YDEN = 0, TX_FORMAT_ACTISENSE } GW_TX_FORMAT;

class CommDriverN2KNet : public QObject,
                         public CommDriverN2K,
                         public DriverStatsProvider {
  Q_OBJECT

public:
  /** Decoded N2K payload handed from the read path to the listener. */
  using N2kPayloadPtr = std::shared_ptr<std::vector<unsigned char>>;

  CommDriverN2KNet(const ConnectionParams* params, DriverListener& listener);

  ~CommDriverN2KNet() override;

  DriverStats GetDriverStats() const override { return m_driver_stats; }

  void SetListener(DriverListener& l) override {};

  void Open();
  void Close();
  ConnectionParams GetParams() const { return m_params; }

  bool SendMessage(std::shared_ptr<const NavMsg> msg,
                   std::shared_ptr<const NavAddr> addr) override;

  // The build defines QT_NO_KEYWORDS (task P1.5a) so the signals/slots/emit
  // macros are off and cannot collide with wx/system headers; Q_SIGNALS /
  // Q_SLOTS / Q_EMIT are used instead. Reverted by task P3.12.
Q_SIGNALS:
  /**
   * Emitted from the read path with one decoded N2K payload. Connected to
   * HandleN2kPayload with Qt::QueuedConnection so the payload is dispatched
   * to upper layers after the socket handler returns (the old AddPendingEvent
   * deferral).
   */
  void N2kMsgReceived(CommDriverN2KNet::N2kPayloadPtr payload);

private Q_SLOTS:
  void OnRxSocketData();        ///< QTcpSocket/QUdpSocket readyRead
  void OnSocketConnected();     ///< QTcpSocket connected
  void OnSocketDisconnected();  ///< QTcpSocket disconnected / error
  void OnServerConnection();    ///< QTcpServer newConnection
  void OnSocketTimer();         ///< reconnect attempt (single-shot)
  void OnWatchdogTimer();       ///< no-data watchdog (1 s continuous)
  void OnProdInfoTimer();       ///< YDEN TX-capability probe (single-shot)
  void HandleN2kPayload(CommDriverN2KNet::N2kPayloadPtr payload);

private:
  ConnectionParams m_params;
  DriverListener& m_listener;

  void OpenNetworkTCP(unsigned int addr);
  void OpenNetworkUDP(unsigned int addr);
  void HandleResume();

  /** Push received bytes through DetectFormat + the matching parser. */
  void ProcessRxBytes(const std::vector<unsigned char>& data, int count);
  /** Wire a (re)connected TCP socket's signals to this driver's slots. */
  void ConnectTcpSocketSignals();

  std::string GetNetPort() const { return m_net_port; }
  std::string GetPort() const { return m_portstring; }
  NetworkProtocol GetProtocol() const { return m_net_protocol; }
  dsPortType GetPortType() const { return m_io_select; }

  std::vector<unsigned char> PushFastMsgFragment(const CanHeader& header,
                                                 int position);
  std::vector<unsigned char> PushCompleteMsg(const CanHeader header,
                                             int position,
                                             const can_frame frame);

  void HandleCanFrameInput(can_frame frame);

  void SetOk(bool ok) { m_bok = ok; };

  N2K_Format DetectFormat(const std::vector<unsigned char>& packet);
  bool ProcessActisense_ASCII_RAW(std::vector<unsigned char> packet);
  bool ProcessActisense_ASCII_N2K(std::vector<unsigned char> packet);
  bool ProcessActisense_N2K(std::vector<unsigned char> packet);
  bool ProcessActisense_RAW(std::vector<unsigned char> packet);
  bool ProcessActisense_NGT(std::vector<unsigned char> packet);
  bool ProcessSeaSmart(std::vector<unsigned char> packet);
  bool ProcessMiniPlex(std::vector<unsigned char> packet);

  bool SendN2KNetwork(std::shared_ptr<const Nmea2000Msg>& msg,
                      std::shared_ptr<const NavAddr2000> dest_addr);

  std::vector<std::vector<unsigned char>> GetTxVector(
      const std::shared_ptr<const Nmea2000Msg>& msg,
      std::shared_ptr<const NavAddr2000> dest_addr);
  bool SendSentenceNetwork(std::vector<std::vector<unsigned char>> payload);
  bool HandleMgntMsg(uint64_t pgn, std::vector<unsigned char>& payload);
  bool PrepareForTX();
  std::vector<unsigned char> PrepareLogPayload(
      std::shared_ptr<const Nmea2000Msg>& msg,
      std::shared_ptr<const NavAddr2000> addr);

  /** Apply TCP_NODELAY and a small send buffer to an output socket. */
  bool SetOutputSocketOptions(QTcpSocket* sock);

  StatsTimer m_stats_timer;
  DriverStats m_driver_stats;

  std::string m_net_port;
  NetworkProtocol m_net_protocol;
  std::string m_host;  ///< target host address (numeric or name)
  bool m_is_multicast;

  // Qt sockets -- owned via QObject parenting to this driver.
  QTcpSocket* m_tcp_socket;  ///< TCP client / GPSD / accepted server peer
  QTcpServer* m_tcp_server;  ///< TCP server (listen mode)
  QUdpSocket* m_rx_socket;   ///< UDP receive socket
  QUdpSocket* m_tx_socket;   ///< UDP transmit socket

  QTimer* m_socket_timer;    ///< reconnect attempt, single-shot
  QTimer* m_watchdog_timer;  ///< no-data watchdog, 1 s continuous
  QTimer* m_prodinfo_timer;  ///< YDEN TX-capability probe, single-shot

  int m_txenter;
  int m_dog_value;
  std::string m_sock_buffer;
  std::string m_portstring;
  dsPortType m_io_select;
  QDateTime m_connect_time;
  bool m_brx_connect_event;
  bool m_bchecksumCheck;
  ConnectionType m_connection_type;

  bool m_bok;
  int m_ib;
  bool m_bInMsg, m_bGotESC, m_bGotSOT;

  CircularBuffer<unsigned char> m_circle;
  unsigned char* rx_buffer;
  std::string m_sentence;

  FastMessageMap* fast_messages;
  N2K_Format m_n2k_format;
  uint8_t m_order;
  char m_TX_flag;
  bool m_TX_available;

  ObsListener resume_listener;
};

#endif  // guard
