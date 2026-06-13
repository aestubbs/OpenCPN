/***************************************************************************
 *   Copyright (C) 2023 by David Register                                  *
 *   Copyright (C) 2026 Alec Leamas                                        *
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
 * Implement comm_drv_n2k_net.h -- IP network nmea2K driver
 */

#include <iomanip>
#include <sstream>
#include <vector>

#include <stdlib.h>
#include <math.h>
#include <time.h>

#ifdef __MINGW32__
#undef IPV6STRICT  // mingw FTBS fix:  missing struct ip_mreq
#include <ws2tcpip.h>
#include <windows.h>
#endif

#ifdef __MSVC__
#include <winsock2.h>
#include <wx/msw/winundef.h>
#include <ws2tcpip.h>
#endif

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/tcp.h>
#endif

#include <cstring>

// Qt headers first -- they must be parsed before wxWidgets/system headers
// pull in macros (Carbon check/verify, X11 Bool/None) that would otherwise
// collide with Qt. See docs/QT_MIGRATION_N2K_NET_PLAN.md.
#include <QByteArray>
#include <QHostAddress>
#include <QMetaType>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/tokenzr.h>
#include <wx/datetime.h>
#include <wx/log.h>

#include "model/comm_drv_n2k_net.h"
#include "model/comm_navmsg_bus.h"
#include "model/idents.h"
#include "model/comm_drv_registry.h"
#include "model/sys_events.h"

#define N_DOG_TIMEOUT 8

using namespace std::literals::chrono_literals;

static const int kNotFound = -1;

/// CAN v2.0 29 bit header as used by NMEA 2000
CanHeader::CanHeader()
    : priority('\0'), source('\0'), destination('\0'), pgn(-1) {};

static uint64_t PayloadToName(const std::vector<unsigned char> payload) {
  uint64_t name;
  memcpy(&name, reinterpret_cast<const void*>(payload.data()), sizeof(name));
  return name;
}

//========================================================================
/*    commdriverN2KNet implementation
 * */

CommDriverN2KNet::CommDriverN2KNet(const ConnectionParams* params,
                                   DriverListener& listener)
    : CommDriverN2K(params->GetStrippedDSPort()),
      m_params(*params),
      m_listener(listener),
      m_stats_timer(*this, 2s),
      m_net_port(std::to_string(params->NetworkPort)),
      m_net_protocol(params->NetProtocol),
      m_host(params->NetworkAddress.ToStdString()),
      m_is_multicast(false),
      m_tcp_socket(nullptr),
      m_tcp_server(nullptr),
      m_rx_socket(nullptr),
      m_tx_socket(nullptr),
      m_socket_timer(nullptr),
      m_watchdog_timer(nullptr),
      m_prodinfo_timer(nullptr),
      m_txenter(0),
      m_dog_value(0),
      m_portstring(params->GetDSPort().ToStdString()),
      m_io_select(params->IOSelect),
      m_brx_connect_event(false),
      m_connection_type(params->Type),
      m_bok(false),
      m_circle(RX_BUFFER_SIZE_NET),
      m_TX_available(false) {
  m_driver_stats.driver_bus = NavAddr::Bus::N2000;
  m_driver_stats.driver_iface = params->GetStrippedDSPort();

  this->attributes["netAddress"] = params->NetworkAddress.ToStdString();
  this->attributes["netPort"] = std::to_string(params->NetworkPort);
  this->attributes["userComment"] = params->UserComment.ToStdString();
  this->attributes["ioDirection"] = DsPortTypeToString(params->IOSelect);

  // Decoded payloads cross a queued connection -- register the metatype.
  qRegisterMetaType<CommDriverN2KNet::N2kPayloadPtr>(
      "CommDriverN2KNet::N2kPayloadPtr");

  // Native Qt signal/slot replacing the old custom wxEvent + AddPendingEvent.
  // QueuedConnection keeps the deferral: the payload is dispatched after the
  // socket read handler returns.
  connect(this, &CommDriverN2KNet::N2kMsgReceived, this,
          &CommDriverN2KNet::HandleN2kPayload, Qt::QueuedConnection);

  // Reconnect timer -- one-shot, restarted as needed.
  m_socket_timer = new QTimer(this);
  m_socket_timer->setSingleShot(true);
  connect(m_socket_timer, &QTimer::timeout, this,
          &CommDriverN2KNet::OnSocketTimer);

  // No-data watchdog -- 1 s continuous.
  m_watchdog_timer = new QTimer(this);
  connect(m_watchdog_timer, &QTimer::timeout, this,
          &CommDriverN2KNet::OnWatchdogTimer);

  // YDEN TX-capability probe -- single-shot.
  m_prodinfo_timer = new QTimer(this);
  m_prodinfo_timer->setSingleShot(true);
  connect(m_prodinfo_timer, &QTimer::timeout, this,
          &CommDriverN2KNet::OnProdInfoTimer);

  m_ib = 0;
  m_bInMsg = false;
  m_bGotESC = false;
  m_bGotSOT = false;
  rx_buffer = new unsigned char[RX_BUFFER_SIZE_NET + 1];

  fast_messages = new FastMessageMap();
  m_order = 0;  // initialize the fast message sequence ID bits, for TX
  m_n2k_format = N2KFormat_YD_RAW;

  // Establish the power events response
  resume_listener.Init(SystemEvents::GetInstance().evt_resume,
                       [&](ObservedEvt&) { HandleResume(); });

  Open();
}

CommDriverN2KNet::~CommDriverN2KNet() {
  Close();
  delete[] rx_buffer;
  // QTimers and Qt sockets are parented to this driver and destroyed with it.
}

typedef struct {
  std::string Model_ID;
  char RT_flag;
} product_info;

std::unordered_map<uint8_t, product_info> prod_info_map;

bool CommDriverN2KNet::HandleMgntMsg(uint64_t pgn,
                                     std::vector<unsigned char>& payload) {
  // Process a few N2K network management messages
  auto name = PayloadToName(payload);
  auto msg =
      std::make_shared<const Nmea2000Msg>(pgn, payload, GetAddress(name));

  bool b_handled = false;
  switch (pgn) {
    case 126996: {  // Product information
      uint8_t src_addr = payload.at(7);
      if (src_addr == 75) return false;  // skip simulator mgnt messages
      product_info pr_info;
      pr_info.Model_ID = std::string((char*)&payload.data()[17], 32);
      pr_info.RT_flag = m_TX_flag;

      prod_info_map[src_addr] = pr_info;
      b_handled = true;
      break;
    }
    case 59904: {  // ISO request
      uint8_t src_addr = payload.at(7);
      b_handled = true;
      break;
    }
    default:
      break;
  }
  return b_handled;
}

void CommDriverN2KNet::OnProdInfoTimer() {
  // Check the results of the PGN 126996 capture
  bool b_found = false;
  for (const auto& [key, value] : prod_info_map) {
    auto prod_info = value;
    if (prod_info.Model_ID.find("YDEN") != std::string::npos) {
      // Found a YDEN device
      // If this configured port is actually connector to YDEN,
      // then the device will have marked the received TCP packet
      // with "T" indicator.  Check it.
      if (prod_info.RT_flag == 'T') b_found = true;
      break;
    }
  }

  if (b_found) m_TX_available = true;
  prod_info_map.clear();
}

void CommDriverN2KNet::HandleN2kPayload(CommDriverN2KNet::N2kPayloadPtr p) {
  std::vector<unsigned char>* payload = p.get();

  // extract PGN
  uint64_t pgn = 0;
  unsigned char* c = (unsigned char*)&pgn;
  *c++ = payload->at(3);
  *c++ = payload->at(4);
  *c++ = payload->at(5);
  // memcpy(&v, &data[3], 1);
  // printf("          %ld\n", pgn);

  auto name = PayloadToName(*payload);
  auto msg =
      std::make_shared<const Nmea2000Msg>(pgn, *payload, GetAddress(name));
  m_driver_stats.rx_count += payload->size();
  m_listener.Notify(std::move(msg));
}

void CommDriverN2KNet::Open() {
  unsigned int addr =
      QHostAddress(QString::fromStdString(m_host)).toIPv4Address();
  switch (m_net_protocol) {
    case TCP:
      OpenNetworkTCP(addr);
      break;
    case UDP:
      OpenNetworkUDP(addr);
      break;
    default:
      break;
  }
  SetOk(true);
}

void CommDriverN2KNet::ConnectTcpSocketSignals() {
  if (!m_tcp_socket) return;
  connect(m_tcp_socket, &QTcpSocket::readyRead, this,
          &CommDriverN2KNet::OnRxSocketData);
  connect(m_tcp_socket, &QTcpSocket::connected, this,
          &CommDriverN2KNet::OnSocketConnected);
  connect(m_tcp_socket, &QTcpSocket::disconnected, this,
          &CommDriverN2KNet::OnSocketDisconnected);
  connect(m_tcp_socket, &QAbstractSocket::errorOccurred, this,
          &CommDriverN2KNet::OnSocketDisconnected);
}

void CommDriverN2KNet::OpenNetworkUDP(unsigned int addr) {
  (void)addr;  // multicast is detected from the host address below
  const quint16 port = static_cast<quint16>(m_params.NetworkPort);
  QHostAddress host_addr(QString::fromStdString(m_host));

  if (GetPortType() != DS_TYPE_OUTPUT) {
    // Datagram receive socket, bound to the configured port on any interface.
    m_rx_socket = new QUdpSocket(this);
    m_rx_socket->bind(QHostAddress::AnyIPv4, port,
                      QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (host_addr.isMulticast()) {
      m_is_multicast = true;
      m_rx_socket->joinMulticastGroup(host_addr);
    }
    connect(m_rx_socket, &QUdpSocket::readyRead, this,
            &CommDriverN2KNet::OnRxSocketData);
    m_driver_stats.available = true;
  }

  // Set up another socket for transmit, on an ephemeral port. Qt enables
  // broadcast as needed when writing to a broadcast address.
  if (GetPortType() != DS_TYPE_INPUT) {
    m_tx_socket = new QUdpSocket(this);
    m_tx_socket->bind(QHostAddress::AnyIPv4, 0);
    m_driver_stats.available = true;
  }

  // In case the connection is lost before acquired....
  m_connect_time = QDateTime::currentDateTime();
}

void CommDriverN2KNet::OpenNetworkTCP(unsigned int addr) {
  int isServer = ((addr == 0u /* INADDR_ANY */) ? 1 : 0);
  wxLogMessage(wxString::Format("Opening TCP Server %d", isServer));

  if (isServer) {
    m_tcp_server = new QTcpServer(this);
    connect(m_tcp_server, &QTcpServer::newConnection, this,
            &CommDriverN2KNet::OnServerConnection);
    if (!m_tcp_server->listen(QHostAddress::AnyIPv4,
                              static_cast<quint16>(m_params.NetworkPort))) {
      wxLogMessage(wxString::Format("N2K TCP server listen failed: port %d",
                                    m_params.NetworkPort));
    }
  } else {
    m_tcp_socket = new QTcpSocket(this);
    ConnectTcpSocketSignals();
    m_brx_connect_event = false;
    m_socket_timer->start(100);  // schedule the connection attempt
  }

  // In case the connection is lost before acquired....
  m_connect_time = QDateTime::currentDateTime();
}

void CommDriverN2KNet::OnWatchdogTimer() {
  m_dog_value--;

  if (m_dog_value <= 0) {  // No receive in n seconds
    if (GetParams().NoDataReconnect) {
      // Reconnect on NO DATA is true, so try to reconnect now.
      if (GetProtocol() == TCP) {
        if (m_tcp_socket) m_tcp_socket->abort();

        int n_reconnect_delay = wxMax(N_DOG_TIMEOUT - 2, 2);
        wxLogMessage(wxString::Format(" Reconnection scheduled in %d seconds.",
                                      n_reconnect_delay));
        m_socket_timer->start(n_reconnect_delay * 1000);

        //  Stop DATA watchdog, will be restarted on successful connection.
        m_watchdog_timer->stop();
      }
    }
  }
}

void CommDriverN2KNet::OnSocketTimer() {
  //  Attempt a connection
  if (m_tcp_socket &&
      m_tcp_socket->state() == QAbstractSocket::UnconnectedState) {
    wxLogDebug(" Attempting reconnection...");
    m_brx_connect_event = false;
    //  Stop DATA watchdog, may be restarted on successful connection.
    m_watchdog_timer->stop();
    m_tcp_socket->connectToHost(QString::fromStdString(m_host),
                                static_cast<quint16>(m_params.NetworkPort));

    // schedule another connection attempt, in case this one fails
    m_socket_timer->start(N_DOG_TIMEOUT * 1000);
  }
}

void CommDriverN2KNet::HandleResume() {
  //  Attempt a stop and restart of connection
  if (m_tcp_socket) {
    m_watchdog_timer->stop();

    m_tcp_socket->abort();

    // schedule reconnect attempt
    int n_reconnect_delay = wxMax(N_DOG_TIMEOUT - 2, 2);
    wxLogMessage(wxString::Format(" Reconnection scheduled in %d seconds.",
                                  n_reconnect_delay));

    m_socket_timer->start(n_reconnect_delay * 1000);
  }
}

bool CommDriverN2KNet::SendMessage(std::shared_ptr<const NavMsg> msg,
                                   std::shared_ptr<const NavAddr> addr) {
  if (!msg) return false;
  auto msg_n2k = std::dynamic_pointer_cast<const Nmea2000Msg>(msg);
  auto dest_addr_n2k = std::static_pointer_cast<const NavAddr2000>(addr);
  return SendN2KNetwork(msg_n2k, dest_addr_n2k);
}

std::vector<unsigned char> CommDriverN2KNet::PrepareLogPayload(
    std::shared_ptr<const Nmea2000Msg>& msg,
    std::shared_ptr<const NavAddr2000> addr) {
  std::vector<unsigned char> data;
  data.push_back(0x94);
  data.push_back(0x13);
  data.push_back(msg->priority);
  data.push_back(msg->PGN.pgn & 0xFF);
  data.push_back((msg->PGN.pgn >> 8) & 0xFF);
  data.push_back((msg->PGN.pgn >> 16) & 0xFF);
  data.push_back(addr->address);
  data.push_back(addr->address);
  for (size_t n = 0; n < msg->payload.size(); n++)
    data.push_back(msg->payload[n]);
  data.push_back(0x55);  // CRC dummy, not checked
  return data;
}

std::vector<unsigned char> CommDriverN2KNet::PushCompleteMsg(
    const CanHeader header, int position, const can_frame frame) {
  std::vector<unsigned char> data;
  data.push_back(0x93);
  data.push_back(0x13);
  data.push_back(header.priority);
  data.push_back(header.pgn & 0xFF);
  data.push_back((header.pgn >> 8) & 0xFF);
  data.push_back((header.pgn >> 16) & 0xFF);
  data.push_back(header.destination);
  data.push_back(header.source);
  data.push_back(0xFF);  // FIXME (dave) generate the time fields
  data.push_back(0xFF);
  data.push_back(0xFF);
  data.push_back(0xFF);
  data.push_back(CAN_MAX_DLEN);  // nominally 8
  for (size_t n = 0; n < CAN_MAX_DLEN; n++) data.push_back(frame.data[n]);
  data.push_back(0x55);  // CRC dummy, not checked
  return data;
}

std::vector<unsigned char> CommDriverN2KNet::PushFastMsgFragment(
    const CanHeader& header, int position) {
  std::vector<unsigned char> data;
  data.push_back(0x93);
  data.push_back(fast_messages->entries[position].expected_length + 11);
  data.push_back(header.priority);
  data.push_back(header.pgn & 0xFF);
  data.push_back((header.pgn >> 8) & 0xFF);
  data.push_back((header.pgn >> 16) & 0xFF);
  data.push_back(header.destination);
  data.push_back(header.source);
  data.push_back(0xFF);  // FIXME (dave) Could generate the time fields
  data.push_back(0xFF);
  data.push_back(0xFF);
  data.push_back(0xFF);
  data.push_back(fast_messages->entries[position].expected_length);
  for (size_t n = 0; n < fast_messages->entries[position].expected_length; n++)
    data.push_back(fast_messages->entries[position].data[n]);
  data.push_back(0x55);  // CRC dummy
  fast_messages->Remove(position);
  return data;
}

/**
 * Handle a frame. A complete message or last part of a multipart fast
 * message is sent to m_listener, basically making it available to upper
 * layers. Otherwise, the fast message fragment is stored waiting for
 * next fragment.
 */
void CommDriverN2KNet::HandleCanFrameInput(can_frame frame) {
  int position = -1;
  bool ready = true;

  CanHeader header(frame);
  if (header.IsFastMessage()) {
    position = fast_messages->FindMatchingEntry(header, frame.data[0]);
    if (position == kNotFound) {
      // Not an existing fast message:
      // If valid, create new entry and insert first frame
      // First, sanity check the arriving frame.
      // If it is not the first frame of a FastMessage, then discard it
      // n.b. This should be considered a network error, or possibly a gateway
      //  error.  Maybe as simple as a dropped starting frame....
      if ((frame.data[0] & 0x1F) == 0) {
        position = fast_messages->AddNewEntry();
        ready = fast_messages->InsertEntry(header, frame.data, position);
      } else
        ready = false;
    } else {
      // An existing fast message entry is present, append the frame
      ready = fast_messages->AppendEntry(header, frame.data, position);
    }
  }
  if (ready) {
    std::vector<unsigned char> vec;
    if (position >= 0) {
      // Re-assembled fast message
      vec = PushFastMsgFragment(header, position);
    } else {
      // Single frame message
      vec = PushCompleteMsg(header, position, frame);
    }

    // Intercept network management messages not used by OCPN navigation core.
    if (HandleMgntMsg(header.pgn, vec)) return;

    // Message is ready -- hand it to the listener via the queued signal.
    emit N2kMsgReceived(std::make_shared<std::vector<unsigned char>>(vec));
  }
}

static bool isASCII(const std::vector<unsigned char>& packet) {
  for (unsigned char c : packet) {
    if (!isascii(c)) return false;
  }
  return true;
}

N2K_Format CommDriverN2KNet::DetectFormat(
    const std::vector<unsigned char>& packet) {
  // A simplistic attempt at identifying which of the various available
  //    on-wire (or air) formats being emitted by a configured
  //    Actisense N2k<->ethernet device.

  if (isASCII(packet)) {
    std::string payload = std::string(packet.begin(), packet.end());
    if (payload.find("$PCDIN") != std::string::npos) {
      return N2KFormat_SeaSmart;
    } else if (payload.find("$MXPGN") != std::string::npos) {
      // TODO: Due to the weird fragmentation observed with default settings of
      // the wi-fi part, the payload does not always start with or even contain
      // `$MXPGN`. We now lose the later.
      return N2KFormat_MiniPlex;
    } else if (std::find(packet.begin(), packet.end(), ':') != packet.end()) {
      return N2KFormat_Actisense_RAW_ASCII;
    } else {
      return N2KFormat_Actisense_N2K_ASCII;
    }
  } else {
    if (packet[2] == 0x95)
      return N2KFormat_Actisense_RAW;
    else if (packet[2] == 0xd0)
      return N2KFormat_Actisense_N2K;
    else if (packet[2] == 0x93)
      return N2KFormat_Actisense_NGT;
  }
  return N2KFormat_Undefined;
}

bool CommDriverN2KNet::ProcessActisense_N2K(std::vector<unsigned char> packet) {
  // 1002 d0 1500ff0401f80900684c1b00a074eb14f89052d288 1003

  std::vector<unsigned char> data;
  bool bInMsg = false;
  bool bGotESC = false;
  bool bGotSOT = false;

  while (!m_circle.IsEmpty()) {
    uint8_t next_byte = m_circle.Get();

    if (bInMsg) {
      if (bGotESC) {
        if (next_byte == ESCAPE) {
          data.push_back(next_byte);
          bGotESC = false;
        } else if (next_byte == ENDOFTEXT) {
          // Process packet
          // first 3 bytes are: 1 byte for message type, 2 bytes for rest of
          // message length
          unsigned int msg_length =
              (uint32_t)data[1] + ((uint32_t)data[2] << 8);

          // As a sanity check, verify message length
          if (msg_length == data.size() - 1) {
            uint8_t destination = data[3];
            uint8_t source = data[4];

            uint8_t dprp = data[7];
            uint8_t priority =
                (dprp >> 2) & 7;        // priority bits are 3,4,5th bit
            uint8_t rAndDP = dprp & 3;  // data page + reserved is first 2 bits

            // PGN
            uint8_t pduFormat = data[6];  // PF (PDU Format)
            uint32_t pgn = (rAndDP << 16) + (pduFormat << 8);
            if (pduFormat >=
                240)  // message is broadcast, PS contains group extension
              pgn += data[5];  // +PS (PDU Specific)

            // Create the OCPN payload
            std::vector<uint8_t> o_payload;
            o_payload.push_back(0x93);
            o_payload.push_back(0x13);
            o_payload.push_back(priority);  // priority;
            o_payload.push_back(pgn & 0xFF);
            o_payload.push_back((pgn >> 8) & 0xFF);
            o_payload.push_back((pgn >> 16) & 0xFF);
            o_payload.push_back(destination);  // destination;
            o_payload.push_back(source);       // source);
            o_payload.push_back(0xFF);  // FIXME (dave) generate the time fields
            o_payload.push_back(0xFF);
            o_payload.push_back(0xFF);
            o_payload.push_back(0xFF);
            o_payload.push_back(data.size());

            // Data starts at offset 13
            for (size_t n = 13; n < data.size() - 1; n++)
              o_payload.push_back(data[n]);

            o_payload.push_back(0x55);  // CRC dummy, not checked

            // Message is ready -- hand it to the listener (queued signal).
            emit N2kMsgReceived(
                std::make_shared<std::vector<unsigned char>>(o_payload));
          }

          // reset for next packet
          bInMsg = false;
          bGotESC = false;
          data.clear();
        } else if (next_byte == STARTOFTEXT) {
          bGotESC = false;
          data.clear();
        } else {
          data.clear();
          bInMsg = false;
          bGotESC = false;
        }
      } else {
        bGotESC = (next_byte == ESCAPE);

        if (!bGotESC) {
          data.push_back(next_byte);
        }
      }
    }

    else {
      if (STARTOFTEXT == next_byte) {
        bGotSOT = false;
        if (bGotESC) {
          bGotSOT = true;
        }
      } else {
        bGotESC = (next_byte == ESCAPE);
        if (bGotSOT) {
          bGotSOT = false;
          bInMsg = true;

          data.push_back(next_byte);
        }
      }
    }
  }  // while

  return true;
}

bool CommDriverN2KNet::ProcessActisense_RAW(std::vector<unsigned char> packet) {
  // 1002 95 0e15870402f8094b  fc e6 20 00 00 ff ff 6f 1003

  can_frame frame;

  std::vector<unsigned char> data;
  bool bInMsg = false;
  bool bGotESC = false;
  bool bGotSOT = false;

  while (!m_circle.IsEmpty()) {
    uint8_t next_byte = m_circle.Get();

    if (bInMsg) {
      if (bGotESC) {
        if (next_byte == ESCAPE) {
          data.push_back(next_byte);
          bGotESC = false;
        } else if (next_byte == ENDOFTEXT) {
          // Process packet
          // Create a can_frame, to assemble fast packets.

          // As a sanity check, verify message length
          if (data.size() >= 8) {
            size_t dLen = data[1];

            if (dLen + 3 == data.size()) {
              // can_id
              memcpy(&frame.can_id, &data.data()[4], 4);

              // data
              memcpy(&frame.data, &data.data()[8], 8);

              HandleCanFrameInput(frame);

              // reset for next packet
              bInMsg = false;
              bGotESC = false;
              data.clear();
            }
          }
        } else if (next_byte == STARTOFTEXT) {
          bGotESC = false;
          data.clear();
        } else {
          data.clear();
          bInMsg = false;
          bGotESC = false;
        }
      } else {
        bGotESC = (next_byte == ESCAPE);

        if (!bGotESC) {
          data.push_back(next_byte);
        }
      }
    }

    else {
      if (STARTOFTEXT == next_byte) {
        bGotSOT = false;
        if (bGotESC) {
          bGotSOT = true;
        }
      } else {
        bGotESC = (next_byte == ESCAPE);
        if (bGotSOT) {
          bGotSOT = false;
          bInMsg = true;

          data.push_back(next_byte);
        }
      }
    }
  }  // while

  return true;
}

bool CommDriverN2KNet::ProcessActisense_NGT(std::vector<unsigned char> packet) {
  std::vector<unsigned char> data;
  bool bInMsg = false;
  bool bGotESC = false;
  bool bGotSOT = false;

  while (!m_circle.IsEmpty()) {
    uint8_t next_byte = m_circle.Get();

    if (bInMsg) {
      if (bGotESC) {
        if (next_byte == ESCAPE) {
          data.push_back(next_byte);
          bGotESC = false;
        } else if (next_byte == ENDOFTEXT) {
          // Process packet -- hand it to the listener (queued signal).
          emit N2kMsgReceived(
              std::make_shared<std::vector<unsigned char>>(data));

          // reset for next packet
          bInMsg = false;
          bGotESC = false;
          data.clear();
        } else if (next_byte == STARTOFTEXT) {
          bGotESC = false;
          data.clear();
        } else {
          data.clear();
          bInMsg = false;
          bGotESC = false;
        }
      } else {
        bGotESC = (next_byte == ESCAPE);

        if (!bGotESC) {
          data.push_back(next_byte);
        }
      }
    }

    else {
      if (STARTOFTEXT == next_byte) {
        bGotSOT = false;
        if (bGotESC) {
          bGotSOT = true;
        }
      } else {
        bGotESC = (next_byte == ESCAPE);
        if (bGotSOT) {
          bGotSOT = false;
          bInMsg = true;

          data.push_back(next_byte);
        }
      }
    }
  }  // while

  return true;
}

bool CommDriverN2KNet::ProcessActisense_ASCII_RAW(
    std::vector<unsigned char> packet) {
  can_frame frame;

  while (!m_circle.IsEmpty()) {
    char b = m_circle.Get();
    if ((b != 0x0a) && (b != 0x0d)) {
      m_sentence += b;
    }
    if (b == 0x0a) {  // end of sentence

      // Extract a can_frame from ASCII stream
      // printf("%s\n", m_sentence.c_str());

      wxString ss(m_sentence.c_str());
      m_sentence.clear();
      wxStringTokenizer tkz(ss, " ");

      // Discard first token
      wxString token = tkz.GetNextToken();  // time stamp

      token = tkz.GetNextToken();  // R/T
      // Record the R/T flag, for use in device detect logic
      m_TX_flag = token[0];

      // can_id;
      token = tkz.GetNextToken();
      long canID;
      token.ToLong(&canID, 16);
      frame.can_id = canID;

      // 8 data bytes, if present, 0 otherwise
      unsigned char bytes[8];
      memset(bytes, 0, 8);
      for (unsigned int i = 0; i < 8; i++) {
        if (tkz.HasMoreTokens()) {
          token = tkz.GetNextToken();
          long tui;
          token.ToLong(&tui, 16);
          bytes[i] = (uint8_t)tui;
        }
      }
      memcpy(&frame.data, bytes, 8);
      HandleCanFrameInput(frame);
    }
  }
  return true;
}

bool CommDriverN2KNet::ProcessActisense_ASCII_N2K(
    std::vector<unsigned char> packet) {
  // A001001.732 04FF6 1FA03 C8FBA80329026400
  std::string sentence;

  while (!m_circle.IsEmpty()) {
    char b = m_circle.Get();
    if ((b != 0x0a) && (b != 0x0d)) {
      sentence += b;
    }
    if (b == 0x0a) {  // end of sentence

      // Extract items
      // printf("%s", sentence.c_str());

      wxString ss(sentence.c_str());
      wxStringTokenizer tkz(ss, " ");
      sentence.clear();  // for next while loop

      // skip timestamp
      wxString time_header = tkz.GetNextToken();

      wxString sprio_addr = tkz.GetNextToken();
      long prio_addr;
      sprio_addr.ToLong(&prio_addr, 16);
      uint8_t priority = (uint8_t)prio_addr & 0X0F;
      uint8_t destination = (uint8_t)(prio_addr >> 4) & 0X0FF;
      uint8_t source = (uint8_t)(prio_addr >> 12) & 0X0FF;

      // PGN
      wxString sPGN = tkz.GetNextToken();
      unsigned long PGN;
      sPGN.ToULong(&PGN, 16);
      // printf("  PGN: %ld\n", PGN);

      // data field
      wxString sdata = tkz.GetNextToken();
      std::vector<uint8_t> data;
      for (size_t i = 0; i < sdata.Length(); i += 2) {
        long dv;
        wxString stui = sdata.Mid(i, 2);
        stui.ToLong(&dv, 16);
        data.push_back((uint8_t)dv);
      }

      // Create the OCPN payload
      std::vector<uint8_t> o_payload;
      o_payload.push_back(0x93);
      o_payload.push_back(0x13);
      o_payload.push_back(priority);  // priority;
      o_payload.push_back(PGN & 0xFF);
      o_payload.push_back((PGN >> 8) & 0xFF);
      o_payload.push_back((PGN >> 16) & 0xFF);
      o_payload.push_back(destination);  // destination;
      o_payload.push_back(source);       // header.source);
      o_payload.push_back(0xFF);  // FIXME (dave) generate the time fields
      o_payload.push_back(0xFF);
      o_payload.push_back(0xFF);
      o_payload.push_back(0xFF);
      o_payload.push_back(data.size());
      for (size_t n = 0; n < data.size(); n++) o_payload.push_back(data[n]);
      o_payload.push_back(0x55);  // CRC dummy, not checked

      if (HandleMgntMsg(PGN, o_payload)) return false;

      // Message is ready -- hand it to the listener (queued signal).
      emit N2kMsgReceived(
          std::make_shared<std::vector<unsigned char>>(o_payload));
    }
  }
  return true;
}

bool CommDriverN2KNet::ProcessSeaSmart(std::vector<unsigned char> packet) {
  while (!m_circle.IsEmpty()) {
    char b = m_circle.Get();
    if ((b != 0x0a) && (b != 0x0d)) {
      m_sentence += b;
    }
    if (b == 0x0a) {  // end of sentence

      // Extract a can_frame from ASCII stream
      // printf("%s\n", m_sentence.c_str());

      wxString ss(m_sentence.c_str());
      m_sentence.clear();
      wxStringTokenizer tkz(ss, ",");

      // Discard first token
      wxString token = tkz.GetNextToken();  // $PCDIN
      m_TX_flag = 'R';

      token = tkz.GetNextToken();  // PGN
      unsigned long PGN;
      token.ToULong(&PGN, 16);

      token = tkz.GetNextToken();  // Timestamp
      unsigned long timestamp;
      token.ToULong(&timestamp, 16);

      token = tkz.GetNextToken();  // Source ID
      unsigned long source;
      token.ToULong(&source, 16);

      token = tkz.GetNextToken();  // Payload + "*CRC_byte"

      wxStringTokenizer datatkz(token, "*");
      wxString data = datatkz.GetNextToken();

      // Create the OCPN payload
      std::vector<uint8_t> o_payload;
      o_payload.push_back(0x93);
      o_payload.push_back(0x13);
      o_payload.push_back(3);  // priority hardcoded, missing in SeaSmart
      o_payload.push_back(PGN & 0xFF);
      o_payload.push_back((PGN >> 8) & 0xFF);
      o_payload.push_back((PGN >> 16) & 0xFF);
      o_payload.push_back(0xFF);  // destination hardcoded, missing in SeaSmart
      o_payload.push_back((uint8_t)source);  // header.source);
      o_payload.push_back(timestamp & 0xFF);
      o_payload.push_back((timestamp >> 8) & 0xFF);
      o_payload.push_back((timestamp >> 16) & 0xFF);
      o_payload.push_back((timestamp >> 24) & 0xFF);
      o_payload.push_back((uint8_t)data.Length() / 2);
      for (size_t i = 0; i < data.Length(); i += 2) {
        unsigned long dv;
        wxString sbyte = data.Mid(i, 2);
        sbyte.ToULong(&dv, 16);
        o_payload.push_back((uint8_t)dv);
      }
      o_payload.push_back(0x55);  // CRC dummy, not checked

      if (HandleMgntMsg(PGN, o_payload)) return false;

      // Message is ready -- hand it to the listener (queued signal).
      emit N2kMsgReceived(
          std::make_shared<std::vector<unsigned char>>(o_payload));
    }
  }
  return true;
}

bool CommDriverN2KNet::ProcessMiniPlex(std::vector<unsigned char> packet) {
  /*
  $MXPGN – NMEA 2000 PGN Data
  This sentence transports NMEA 2000/CAN frames in NMEA 0183 format. The
  MiniPlex-3 will transmit this sentence with Talker ID “MX”. When sent to the
  MiniPlex-3, the Talker ID is ignored unless a routing entry exists for this
  sentence.

  Format: $--PGN,pppppp,aaaa,c--c*hh<CR><LF>

  pppppp: PGN of the NMEA 2000/CAN frame, 3-byte hexadecimal number. If the PGN
  is non-global, the lowest byte contains the destination address. aaaa:
  Attribute Word, a 16-bit hexadecimal number. This word contains the priority,
  the DLC code and then source/destination address of the frame, formatted as
  shown below:

  15    14 13 12   11 10   9   8   7   6   5   4   3   2   1   0
  ----------------------------------------------------------------
  | S | Priority |      DLC      |             Address           |
  ----------------------------------------------------------------

  S: Send bit. When an NMEA 2000/CAN frame is received, this bit is 0.
  To use the $MXPGN sentence to send an NMEA 2000/CAN frame, this bit must be 1.
  Priority: Frame priority. A value between 0 and 7, a lower value means higher
  priority. DLC: Data Length Code field, contains the size of the frame in bytes
  (1..8) or a Class 2 Transmission ID (9..15). Address: Depending on the Send
  bit, this field contains the Source Address (S=0) or the Destination Address
  (S=1) of the frame. c--c: Data field of the NMEA 2000/CAN frame, organised as
  one large number in hexadecimal notation from MSB to LSB. This is in
  accordance with “NMEA 2000 Appendix D”, chapter D.1, “Data Placement within
  the CAN Frame”. The size of this field depends on the DLC value and can be 1
  to 8 bytes (2 to 16 hexadecimal characters).

  NMEA 2000 Reception

  When the MiniPlex-3 converts an NMEA 2000/CAN frame into an $MXPGN sentence,
  the S bit in the Attribute field will be 0 and the Address field contains the
  source address of the frame. The destination address of the frame is either
  global or contained in the lower byte of the PGN, in accordance with the NMEA
  2000/ISO specification.

  Notes:

  Multiple messages can be delivered in a single packet
  It is not guaranteed that the whole message will be delivered in a single
  packet, actually it is common that the last message is split "anywhere" and
  continues in the next packet.

  packet 1 payload

  "$MXPGN,01F119,3816,FFFAAF01A3FDE301*14\r\n
  $MXPGN,01F201,2816,C50E0A19A0001A40*66\r\n
  $MXPGN,01F201,2816,6B4C0039058D8A41*15\r\n
  $MXPGN,01F201,2816,FFFFFFFFFF007542*1D\r\n
  $MXPGN,01F201,2816,FF7F7F0000000A43*6F\r\n
  $MXPGN,01F209,2816,2D002400ED0009A0*18\r\n
  $MXPGN,01F209,2816,FFFFFFFF002C00A1*10\r\n
  $MXPGN,01F213,6816,00B4F512020106C0*6E\r\n
  $MXPGN,01F214,6816,01FFFF7FFF04F801*12\r\n
  $MXPGN,01F214,6816,7EFFFF0009056400*65\r\n
  $MXPGN,"

  packet 2 payload

  "01F212,6816,185B560101010BC0*62\r\n
  $MXPGN,01F212,6816,FFFFFFFF00D700C1*1E\r\n
  $MXPGN,01FD06,5816,FF03F6749570C101*67\r\n
  $MXPGN,01FD07,5816,03F635B672F20401*1B\r\n"

  packet 1

  "$MXPGN,01F114,3816,FFFFF000D20212FF*1E\r\n
  $MXPGN,01F905,6816,0001000300005BC0*14\r\n
  $MXPGN,01F905,6816,6142010EE00007C1*67\r\n
  $MXPGN,01F905,6816,68206F74206B63C2*6F\r\n
  $MXPGN,01F905,6816,0D0001FF656D6FC3*16\r\n
  $MXPGN,01F905,6816,20747261745301C4*62\r\n
  $MXPGN,01F905,6816,4600746E696F70C5*6E\r\n
  $MXPGN,01F905,6816,020C84588023C3C6*6E\r\n
  $MXPGN,01F905,6816,6E727554011200C7*11\r\n
  $MXPGN,01F905,6816,65726F666562"

  packet 2 payload

  "20C8*1A\r\n
  $MXPGN,01F905,6816,CCA06B636F7220C9*1F\r\n
  $MXPGN,01F905,6816,030C85DF2023C4CA*1B\r\n
  $MXPGN,01F905,6816,656D6F48010600CB*19\r\n
  $MXPGN,01F905,6816,8765C023C65340CC*1B\r\n
  $MXPGN,01F905,6816,FFFFFFFFFFFF0CCD*66\r\n
  $MXPGN,01F10D,2816,FFFF0369FC97F901*16\r\n
  $MXPGN,01F112,2816,FD03C0FDF49B1A00*11\r\n
  $MXPGN,01F200,2816,FFFF7FFFFF43F800*10\r\n
  $MXPGN,01F205,2816,FF050D3A1D4CFC00*19\r\n"
  */
  while (!m_circle.IsEmpty()) {
    char b = m_circle.Get();
    if ((b != 0x0a) && (b != 0x0d)) {
      m_sentence += b;
    }
    if (b == 0x0a) {  // end of sentence

      // Extract a can_frame from ASCII stream
      // printf("%s\n", m_sentence.c_str());

      wxString ss(m_sentence.c_str());
      m_sentence.clear();
      wxStringTokenizer tkz(ss, ",");

      // Discard first token
      wxString token = tkz.GetNextToken();  // $MXPGN
      m_TX_flag = 'R';

      token = tkz.GetNextToken();  // PGN
      unsigned long PGN;
      token.ToULong(&PGN, 16);

      token = tkz.GetNextToken();  // Attribute compound field
      unsigned long attr;
      token.ToULong(&attr, 16);
      // Send Bit
      bool send_bit = (attr >> 15) != 0;
      // Priority
      uint8_t priority = (attr >> 12) & 0x07;

      // dlc
      uint8_t dlc = (attr >> 8) & 0x0F;

      // address
      uint8_t address = attr & 0xFF;

      token = tkz.GetNextToken();  // Payload + "*CRC_byte"

      wxStringTokenizer datatkz(token, "*");
      wxString data = datatkz.GetNextToken();

      if (data.Length() >
          16) {  // Payload can never exceed 8 bytes (=16 HEX characters)
        return false;
      }

      can_frame frame;
      memset(&frame.data, 0, 8);
      for (size_t i = 0; i < data.Length(); i += 2) {
        unsigned long dv;
        wxString sbyte = data.Mid(data.Length() - i - 2, 2);
        sbyte.ToULong(&dv, 16);
        frame.data[i / 2] = ((uint8_t)dv);
      }
      frame.can_id = (uint32_t)BuildCanID(priority, address, 0xFF, PGN);
      HandleCanFrameInput(frame);
    }
  }
  return true;
}

void CommDriverN2KNet::ProcessRxBytes(const std::vector<unsigned char>& rx_data,
                                      int count) {
  m_driver_stats.available = true;

  // DetectFormat() inspects the whole buffer; give it a NUL-terminated copy,
  // as the wx implementation did.
  std::vector<unsigned char> data = rx_data;
  if (count >= 0 && count < static_cast<int>(data.size())) data[count] = 0;

  for (int i = 0; i < count; i++) {
    if (!m_circle.IsFull()) m_circle.Put(data[i]);
  }

  m_n2k_format = DetectFormat(data);

  switch (m_n2k_format) {
    case N2KFormat_Actisense_RAW_ASCII:
      ProcessActisense_ASCII_RAW(data);
      break;
    case N2KFormat_YD_RAW:  // RX Byte compatible with Actisense ASCII RAW
      ProcessActisense_ASCII_RAW(data);
      break;
    case N2KFormat_Actisense_N2K_ASCII:
      ProcessActisense_ASCII_N2K(data);
      break;
    case N2KFormat_Actisense_N2K:
      ProcessActisense_N2K(data);
      break;
    case N2KFormat_Actisense_RAW:
      ProcessActisense_RAW(data);
      break;
    case N2KFormat_Actisense_NGT:
      ProcessActisense_NGT(data);
      break;
    case N2KFormat_SeaSmart:
      ProcessSeaSmart(data);
      break;
    case N2KFormat_MiniPlex:
      ProcessMiniPlex(data);
      break;
    case N2KFormat_Undefined:
    default:
      break;
  }

  m_dog_value = N_DOG_TIMEOUT;  // feed the dog
}

void CommDriverN2KNet::OnRxSocketData() {
  const int RD_BUF_SIZE = 4096;

  if (GetProtocol() == UDP) {
    while (m_rx_socket && m_rx_socket->hasPendingDatagrams()) {
      std::vector<unsigned char> data(RD_BUF_SIZE + 1, 0);
      qint64 n = m_rx_socket->readDatagram(
          reinterpret_cast<char*>(data.data()), RD_BUF_SIZE);
      if (n > 0) ProcessRxBytes(data, static_cast<int>(n));
    }
  } else {
    if (!m_tcp_socket) return;
    while (m_tcp_socket->bytesAvailable() > 0) {
      std::vector<unsigned char> data(RD_BUF_SIZE + 1, 0);
      qint64 n = m_tcp_socket->read(reinterpret_cast<char*>(data.data()),
                                    RD_BUF_SIZE);
      if (n <= 0) break;
      ProcessRxBytes(data, static_cast<int>(n));
    }
  }
}

void CommDriverN2KNet::OnSocketConnected() {
  m_driver_stats.available = true;
  if (GetProtocol() == GPSD) {
    //  Sign up for watcher mode, Cooked NMEA
    const char cmd[] = "?WATCH={\"class\":\"WATCH\", \"nmea\":true}";
    if (m_tcp_socket)
      m_tcp_socket->write(cmd, static_cast<qint64>(strlen(cmd)));
  } else if (GetProtocol() == TCP) {
    wxLogMessage(wxString::Format(
        "TCP NetworkDataStream connection established: %s", GetPort().c_str()));
    m_dog_value = N_DOG_TIMEOUT;  // feed the dog
    if (GetPortType() != DS_TYPE_OUTPUT) {
      // start the DATA watchdog only if NODATA Reconnect is desired
      if (GetParams().NoDataReconnect) m_watchdog_timer->start(1000);
    }
    if (GetPortType() != DS_TYPE_INPUT && m_tcp_socket)
      (void)SetOutputSocketOptions(m_tcp_socket);
    m_socket_timer->stop();
    m_brx_connect_event = true;
  }
  m_connect_time = QDateTime::currentDateTime();
}

void CommDriverN2KNet::OnSocketDisconnected() {
  m_driver_stats.available = false;
  if (GetProtocol() != TCP && GetProtocol() != GPSD) return;

  if (m_brx_connect_event)
    wxLogMessage(wxString::Format("NetworkDataStream connection lost: %s",
                                  GetPort().c_str()));

  // Server mode: the dropped socket is an accepted peer -- discard it and
  // wait for the next inbound connection.
  if (m_tcp_server) {
    if (m_tcp_socket) {
      m_tcp_socket->deleteLater();
      m_tcp_socket = nullptr;
    }
    return;
  }

  // Client mode: schedule a reconnect attempt.
  qint64 since_connect = 10;  // assumed, if connect time is uninitialized
  if (m_connect_time.isValid())
    since_connect = m_connect_time.secsTo(QDateTime::currentDateTime());

  int retry_time = 5000;  // default
  // If the socket has never connected, and it is a short interval since the
  // connect request, stretch the time a bit.
  if (!m_brx_connect_event && since_connect < 5) retry_time = 10000;

  m_watchdog_timer->stop();
  m_socket_timer->start(retry_time);  // schedule a re-connect attempt
}

void CommDriverN2KNet::OnServerConnection() {
  m_driver_stats.available = true;
  if (!m_tcp_server) return;

  QTcpSocket* peer = m_tcp_server->nextPendingConnection();
  if (!peer) return;

  // One active peer at a time, mirroring the wx implementation.
  if (m_tcp_socket) m_tcp_socket->deleteLater();
  m_tcp_socket = peer;
  m_tcp_socket->setParent(this);
  ConnectTcpSocketSignals();

  if (GetPortType() != DS_TYPE_INPUT) SetOutputSocketOptions(m_tcp_socket);
}

std::vector<unsigned char> MakeSimpleOutMsg(
    int data_format, int pgn, std::vector<unsigned char>& payload) {
  std::vector<unsigned char> out_vec;

  switch (data_format) {
    case N2KFormat_YD_RAW:
    case N2KFormat_Actisense_RAW_ASCII: {
      // Craft the canID
      unsigned can_id = BuildCanID(6, 0xff, 0xff, pgn);
      std::stringstream ss;
      ss << std::setfill('0') << std::setw(8) << std::hex << can_id;
      for (unsigned char s : ss.str()) out_vec.push_back(s);
      out_vec.push_back(' ');

      // Data payload
      std::string sspl;
      char tv[4];
      for (unsigned char d : payload) {
        snprintf(tv, 4, "%02X ", d);
        sspl += tv;
      }
      for (unsigned char s : sspl) out_vec.push_back(s);

      // terminate
      out_vec.pop_back();
      out_vec.push_back(0x0d);
      out_vec.push_back(0x0a);
      break;
    }
    case N2KFormat_Actisense_N2K_ASCII: {
      // Create the time field
      QDateTime now = QDateTime::currentDateTime();
      wxString stime = now.toString("HHmmss").toStdString();
      stime += ".000 ";
      std::string sstime = stime.ToStdString();
      out_vec.push_back('A');
      for (unsigned char s : sstime) out_vec.push_back(s);

      // src/dest/prio field
      wxString sdp;
      sdp.Printf("%02X%02X%1X ",
                 1,  // source
                 (unsigned char)0xFF, 0x6);
      std::string ssdp = sdp.ToStdString();
      for (unsigned char s : ssdp) out_vec.push_back(s);

      //  PGN field
      wxString spgn;
      spgn.Printf("%05X ", pgn);
      std::string sspgn = spgn.ToStdString();
      for (unsigned char s : sspgn) out_vec.push_back(s);

      // Data payload
      std::string sspl;
      char tv[3];
      for (unsigned char d : payload) {
        snprintf(tv, 3, "%02X", d);
        sspl += tv;
      }
      for (unsigned char s : sspl) out_vec.push_back(s);

      // terminator
      out_vec.push_back(0x0d);
      out_vec.push_back(0x0a);
      break;
    }
    case N2KFormat_MiniPlex: {
      out_vec.push_back('$');
      out_vec.push_back('M');
      out_vec.push_back('X');
      out_vec.push_back('P');
      out_vec.push_back('G');
      out_vec.push_back('N');
      out_vec.push_back(',');
      //  PGN field
      wxString spgn;
      spgn.Printf("%06X,", pgn);
      std::string sspgn = spgn.ToStdString();
      for (unsigned char c : sspgn) {
        out_vec.push_back(c);
      }
      // Attribute word
      uint16_t attr = 0;
      uint8_t len = 8;

      attr |= ((uint16_t)0x06) << 12;
      attr |= ((uint16_t)payload.size()) << 8;
      attr |= (uint16_t)0xFF;
      attr |= 0x8000;  // S bit set to 1

      wxString sattr;
      sattr.Printf("%04X,", attr);
      std::string ssattr = sattr.ToStdString();
      for (unsigned char c : ssattr) {
        out_vec.push_back(c);
      }
      // Data payload
      char tv[3];
      for (auto rit = payload.rbegin(); rit != payload.rend(); ++rit) {
        snprintf(tv, 3, "%02X", *rit);
        out_vec.push_back(tv[0]);
        out_vec.push_back(tv[1]);
      }
      // CRC
      uint8_t crc = 0;
      for (auto ci = ++out_vec.begin(); ci != out_vec.end(); ci++) {
        crc ^= *ci;
      }
      out_vec.push_back('*');
      snprintf(tv, 3, "%02X", crc);
      out_vec.push_back(tv[0]);
      out_vec.push_back(tv[1]);

      // term
      out_vec.push_back(0x0d);
      out_vec.push_back(0x0a);
      // DBG: std::cout << std::string(out_vec.begin(), out_vec.end()) <<
      // std::endl << std::flush;
      break;
    }
    default:
      break;
  }
  return out_vec;
}

std::vector<std::vector<unsigned char>> CommDriverN2KNet::GetTxVector(
    const std::shared_ptr<const Nmea2000Msg>& msg,
    std::shared_ptr<const NavAddr2000> dest_addr) {
  std::vector<std::vector<unsigned char>> tx_vector;

  // Branch based on detected network data format currently in use
  switch (m_n2k_format) {
    case N2KFormat_YD_RAW:
      break;
    case N2KFormat_Actisense_RAW_ASCII: {
      //  00:34:02.718 R 15FD0800 FF 00 01 CA 6F FF FF FF
      if (!IsFastMessagePGN(msg->PGN.pgn) && msg->payload.size() <= 8) {
        // Single packet message
        std::vector<unsigned char> header_vec;
        std::vector<unsigned char> out_vec;

        // Craft the canID
        // No need to specify the source address
        // The TX frame will adopt the gateway's claimed N2K address.
        unsigned long can_id =
            BuildCanID(msg->priority, 0, dest_addr->address, msg->PGN.pgn);

        std::stringstream ss;
        ss << std::setfill('0') << std::setw(8) << std::hex << can_id;
        for (unsigned char s : ss.str()) header_vec.push_back(s);
        header_vec.push_back(' ');

        // constant header
        for (unsigned char s : header_vec) out_vec.push_back(s);

        // single data packet
        std::string ssdata;
        for (unsigned int k = 0; k < msg->payload.size(); k++) {
          char tb[4];
          snprintf(tb, 4, "%02X ", msg->payload.data()[k]);
          ssdata += tb;
        }
        for (unsigned char s : ssdata) out_vec.push_back(s);
        out_vec.pop_back();  // drop the last space character

        out_vec.push_back(0x0d);  // terminate the string
        out_vec.push_back(0x0a);

        tx_vector.push_back(out_vec);
      } else {
        std::vector<unsigned char> header_vec;
        std::vector<unsigned char> out_vec;

        // No Need to create a timestamp or frame R/T indicator
#if 0
        // time header
        QDateTime now = QDateTime::currentDateTime();
        wxString stime = now.toString("HH:mm:ss").toStdString();
        stime += ".000 ";
        std::string sstime = stime.ToStdString();
        for (unsigned char s : sstime) header_vec.push_back(s);

        // Tx indicator
        header_vec.push_back('T');
        header_vec.push_back(' ');
#endif

        // Craft the canID
        // No need to specify the source address
        // The TX frame will adopt the gateway's claimed N2K address.
        unsigned long can_id =
            BuildCanID(msg->priority, 0, dest_addr->address, msg->PGN.pgn);
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(8) << std::hex << can_id;
        for (unsigned char s : ss.str()) header_vec.push_back(s);
        header_vec.push_back(' ');

        // format the required number of short packets, in a loop
        int payload_size = msg->payload.size();
        unsigned char temp[8];  // {0,0,0,0,0,0,0,0};
        int cur = 0;
        int nframes =
            (payload_size > 6 ? (payload_size - 6 - 1) / 7 + 1 + 1 : 1);
        bool result = true;
        for (int i = 0; i < nframes && result; i++) {
          temp[0] = i | m_order;  // frame counter
          if (i == 0) {
            temp[1] = msg->payload.size();  // total bytes in fast packet
            // send the first 6 bytes
            for (int j = 2; j < 8; j++) {
              temp[j] = msg->payload.data()[cur];
              cur++;
            }
          } else {
            int j = 1;
            // send the next 7 data bytes
            for (; j < 8 && cur < payload_size; j++) {
              temp[j] = msg->payload.data()[cur];
              cur++;
            }
            for (; j < 8; j++) {
              temp[j] = 0xff;
            }
          }

          out_vec.clear();

          // constant header
          for (unsigned char s : header_vec) out_vec.push_back(s);

          // data, per packet
          std::string ssdata;
          for (unsigned int k = 0; k < 8; k++) {
            char tb[4];
            snprintf(tb, 4, "%02X ", temp[k]);
            ssdata += tb;
          }
          for (unsigned char s : ssdata) out_vec.push_back(s);
          out_vec.pop_back();  // drop the last space character

          out_vec.push_back(0x0d);  // terminate the string
          out_vec.push_back(0x0a);

          tx_vector.push_back(out_vec);
        }  // for loop
      }
    } break;
    case N2KFormat_Actisense_N2K_ASCII: {
      // Source: Actisense own documentation `NMEA 2000 ASCII Output
      // format.docx`
      //
      // Ahhmmss.ddd <SS><DD><P> <PPPPP> b0b1b2b3b4b5b6b7.....bn<CR><LF>
      // A = message is N2K or J1939 message
      // 173321.107 - time 17:33:21.107
      // <SS> - source address
      // <DD> - destination address
      // <P> - priority
      // <PPPPP> - PGN number
      // b0b1b2b3b4b5b6b7.....bn - data payload in hex. NB: ISO TP payload could
      // be up to 1786 bytes
      //
      // Example: `A173321.107 23FF7 1F513 012F3070002F30709F\n`
      //                      1     2     3                  4

      std::vector<unsigned char> ovec;

      // Create the time field
      QDateTime now = QDateTime::currentDateTime();
      wxString stime = now.toString("HHmmss").toStdString();
      stime += ".000 ";
      std::string sstime = stime.ToStdString();
      ovec.push_back('A');
      for (unsigned char s : sstime) ovec.push_back(s);

      // src/dest/prio field
      wxString sdp;
      sdp.Printf("%02X%02X%1X ",
                 1,  // source
                 (unsigned char)dest_addr->address,
                 (unsigned char)msg->priority);
      std::string ssdp = sdp.ToStdString();
      for (unsigned char s : ssdp) ovec.push_back(s);

      //  PGN field
      wxString spgn;
      spgn.Printf("%05X ", (int)msg->PGN.pgn);
      std::string sspgn = spgn.ToStdString();
      for (unsigned char s : sspgn) ovec.push_back(s);

      // Data payload
      std::string sspl;
      char tv[3];
      for (unsigned char d : msg->payload) {
        snprintf(tv, 3, "%02X", d);
        sspl += tv;
      }
      for (unsigned char s : sspl) ovec.push_back(s);

      // term
      ovec.push_back(0x0d);
      ovec.push_back(0x0a);

      // form the result
      tx_vector.push_back(ovec);

      break;
    }
    case N2KFormat_MiniPlex: {
      std::vector<unsigned char> ovec;
      if (!IsFastMessagePGN(msg->PGN.pgn) && msg->payload.size() < 8) {
        // Single packet
      } else {
        size_t cur = 0;
        size_t nframes =
            (msg->payload.size() > 6 ? (msg->payload.size() - 6 - 1) / 7 + 1 + 1
                                     : 1);
        for (size_t i = 0; i < nframes; i++) {
          ovec.push_back('$');
          ovec.push_back('M');
          ovec.push_back('X');
          ovec.push_back('P');
          ovec.push_back('G');
          ovec.push_back('N');
          ovec.push_back(',');
          //  PGN field
          wxString spgn;
          spgn.Printf("%06X,", (int)msg->PGN.pgn);
          std::string sspgn = spgn.ToStdString();
          for (unsigned char c : sspgn) {
            ovec.push_back(c);
          }
          // Attribute word
          uint16_t attr = 0;
          uint8_t len = 8;
          if (i == nframes - 1) {
            // TODO Check this
            // len = msg->payload.size() + 1 - 6 - (nframes - 2) * 7;
          }

          attr |= ((uint16_t)((uint8_t)msg->priority & 0x07)) << 12;
          attr |= ((uint16_t)len) << 8;
          attr |= (uint16_t)dest_addr->address;
          attr |= 0x8000;  // S bit set to 1

          wxString sattr;
          sattr.Printf("%04X,", attr);
          std::string ssattr = sattr.ToStdString();
          for (unsigned char c : ssattr) {
            ovec.push_back(c);
          }
          // Data payload
          char tv[3];
          uint8_t databytes = i == 0 ? len - 2 : len - 1;
          std::vector<unsigned char> payload;
          for (uint8_t j = 0; j < databytes; j++) {
            payload.push_back(msg->payload[cur]);
            cur++;
          }

          // Buffer the data to 7 bytes, if necessary, buffer at start.
          int psize = payload.size();
          while ((i > 0) && (psize < 7)) {
            ovec.push_back('F');
            ovec.push_back('F');
            psize++;
          }

          // Buffer the actual payload bytes
          for (auto rit = payload.rbegin(); rit != payload.rend(); ++rit) {
            snprintf(tv, 3, "%02X", *rit);
            ovec.push_back(tv[0]);
            ovec.push_back(tv[1]);
          }
          if (i == 0) {  // First frame contains the total payload length
            snprintf(tv, 3, "%02X", (uint8_t)msg->payload.size());
            ovec.push_back(tv[0]);
            ovec.push_back(tv[1]);
          }
          // frame counter
          snprintf(tv, 3, "%02X", (uint8_t)i | m_order);
          ovec.push_back(tv[0]);
          ovec.push_back(tv[1]);

          // CRC
          uint8_t crc = 0;
          for (auto ci = ++ovec.begin(); ci != ovec.end(); ci++) {
            crc ^= *ci;
          }
          ovec.push_back('*');
          snprintf(tv, 3, "%02X", crc);
          ovec.push_back(tv[0]);
          ovec.push_back(tv[1]);

          // term
          ovec.push_back(0x0d);
          ovec.push_back(0x0a);

          // DBG: std::cout << std::string(ovec.begin(), ovec.end()) <<
          // std::endl << std::flush;

          // form the result
          tx_vector.push_back(ovec);
          ovec.clear();
        }
        break;
      }
    }
    case N2KFormat_Actisense_N2K:
      break;
    case N2KFormat_Actisense_RAW:
      break;
    case N2KFormat_Actisense_NGT:
      break;
    case N2KFormat_SeaSmart:
      break;
    default:
      break;
  }

  // update the fast message Sequence ID bits
  m_order = (m_order + 0x20) & 0xE0;

  return tx_vector;
}

bool CommDriverN2KNet::PrepareForTX() {
  // We need to determine several items before TX operations can commence.
  // 1.  Is the gateway configured at my ip present, and if so, which of
  //     the two supported gateways is it?  (YDEN-type, or Actisense-type.
  // 2.  If Actisense type, we need to infer the N2K source address it has
  //     claimed, so that we can use that address for our TX operations.

  //  BASIC ASSUMPTION:  There is (or has been) enough network traffic to
  //  allow occurate determination of data format currently in use

  bool b_found = false;

  // Step 1.1
  // If the detected data format is N2KFormat_Actisense_N2K_ASCII,
  // then we are clearly connected to an actisense device.
  // Nothing else need be done.

  if (m_n2k_format == N2KFormat_Actisense_N2K_ASCII) return true;

  // Step 1.2
  // If the detected data format is N2KFormat_MiniPlex,
  // then we are clearly connected to a MiniPlex.
  // Nothing else need be done.

  if (m_n2k_format == N2KFormat_MiniPlex) return true;

  // Step 1.2
  // If the detected data format is N2KFormat_SeaSmart,
  // then we can't transmit.
  if (m_n2k_format == N2KFormat_SeaSmart) return false;

  // Step 2

  //  Assume that the gateway is YDEN type, RAW mode. Verify if true.
  //  Logic:  Actisense gateway will not respond to TX_FORMAT_YDEN,
  //  so if we get sensible response, the gw must be YDEN type.

  // Already tested and found available?
  if (m_TX_available)
    return true;
  else {
    // Send a broadcast request for PGN 126996, Product Information
    std::vector<unsigned char> payload;
    payload.push_back(0x14);
    payload.push_back(0xF0);
    payload.push_back(0x01);

    std::vector<std::vector<unsigned char>> out_data;
    std::vector<unsigned char> msg_vec =
        MakeSimpleOutMsg(N2KFormat_YD_RAW, 59904, payload);
    out_data.push_back(msg_vec);
    SendSentenceNetwork(out_data);

    // Wait some time, and study results
    m_prodinfo_timer->start(200);
  }

  //  No acceptable TX device found
  return false;
}

bool CommDriverN2KNet::SendN2KNetwork(std::shared_ptr<const Nmea2000Msg>& msg,
                                      std::shared_ptr<const NavAddr2000> addr) {
  PrepareForTX();

  std::vector<std::vector<unsigned char>> out_data = GetTxVector(msg, addr);
  SendSentenceNetwork(out_data);
  m_driver_stats.tx_count += msg->payload.size();

  // Create internal message and notify upper layers
  std::vector<unsigned char> msg_payload = PrepareLogPayload(msg, addr);
  m_listener.Notify(
      std::make_shared<const Nmea2000Msg>(msg->PGN.pgn, msg_payload, addr));

  return true;
};

bool CommDriverN2KNet::SendSentenceNetwork(
    std::vector<std::vector<unsigned char>> payload) {
  if (m_txenter)
    return false;  // do not allow recursion, could happen with non-blocking
                   // sockets
  m_txenter++;

  bool ret = true;
  switch (GetProtocol()) {
    case TCP:
      for (std::vector<unsigned char>& v : payload) {
        if (m_tcp_socket &&
            m_tcp_socket->state() == QAbstractSocket::ConnectedState) {
          m_driver_stats.available = true;
          qint64 written = m_tcp_socket->write(
              reinterpret_cast<const char*>(v.data()),
              static_cast<qint64>(v.size()));
          m_tcp_socket->flush();
          m_dog_value = N_DOG_TIMEOUT;  // feed the dog
          if (written < 0) {
            if (m_tcp_server) {
              // Server-side peer write failed -- discard the peer socket.
              m_tcp_socket->deleteLater();
              m_tcp_socket = nullptr;
            } else {
              m_tcp_socket->abort();
              if (!m_socket_timer->isActive())
                m_socket_timer->start(5000);  // schedule a reconnect
              m_watchdog_timer->stop();
            }
            ret = false;
          }
          wxMilliSleep(2);
        } else {
          m_driver_stats.available = false;
          ret = false;
        }
      }
      break;
    case UDP:
      // UDP transmit is intentionally not implemented (matches the previous
      // wx implementation, where the UDP send path was disabled).
      break;

    case GPSD:
    default:
      ret = false;
      break;
  }
  m_txenter--;
  return ret;
}

void CommDriverN2KNet::Close() {
  wxLogMessage(wxString::Format("Closing NMEA NetworkDataStream %s",
                                GetNetPort().c_str()));
  m_stats_timer.Stop();
  if (m_socket_timer) m_socket_timer->stop();
  if (m_watchdog_timer) m_watchdog_timer->stop();
  if (m_prodinfo_timer) m_prodinfo_timer->stop();

  if (m_rx_socket) {
    if (m_is_multicast)
      m_rx_socket->leaveMulticastGroup(
          QHostAddress(QString::fromStdString(m_host)));
    m_rx_socket->close();
  }
  if (m_tx_socket) m_tx_socket->close();
  if (m_tcp_socket) m_tcp_socket->abort();
  if (m_tcp_server) m_tcp_server->close();

  m_driver_stats.available = false;
  // The Qt sockets/timers are parented to this driver and freed with it.
}

bool CommDriverN2KNet::SetOutputSocketOptions(QTcpSocket* sock) {
  if (!sock) return false;

  // Disable the Nagle algorithm on the outgoing connection.
  sock->setSocketOption(QAbstractSocket::LowDelayOption, QVariant(1));

  //  Drastically reduce the size of the socket output buffer so that when a
  //  client goes away without properly closing, the stream quickly fills the
  //  output buffer and thus fails the write() within a few seconds.
  sock->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption,
                        QVariant(1024));
  return true;
}
