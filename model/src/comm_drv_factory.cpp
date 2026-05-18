/**************************************************************************
 *   Copyright (C) 2022 David Register                                     *
 *   Copyright (C) 2022 Alec Leamas                                        *
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
 * Implement comm_drv_factory.h: Communication driver factory.
 */

// FIXME  Why is this needed?
#ifdef _WIN32
#include <winsock2.h>
#include <wx/msw/winundef.h>
#endif

#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif  // precompiled headers

#include <ixwebsocket/IXNetSystem.h>

#include "model/comm_util.h"
#include "model/comm_drv_generic.h"
#include "model/comm_drv_loopback.h"
#include "model/comm_drv_n2k_net.h"
#include "model/comm_n0183_decoder.h"
#include "model/comm_n2k_decoder.h"
#include "model/comm_n2k_gateway_mgr.h"
#include "model/comm_navmsg_bus.h"
#include "model/comm_drv_registry.h"
#include "model/ds_porttype.h"

// SignalK (comm_drv_signalk*) and SocketCAN (comm_drv_n2k_socketcan) are
// parked: their source stays in the tree but is not built and the factory
// no longer creates them. See P1.5m in QT_MIGRATION_TASKS.md.

class N0183Listener : public DriverListener {
public:
  N0183Listener() = default;

  /** Handle driver status change. */
  void Notify(const AbstractCommDriver& driver) override {}

  void Notify(std::shared_ptr<const NavMsg> message) override {
    switch (message->state) {
      case NavMsg::State::kCannotParse:
      case NavMsg::State::kFiltered:
      case NavMsg::State::kBadChecksum:
        CommDriverRegistry::GetInstance().evt_dropped_msg.Notify(message);
        break;
      default:
        NavMsgBus::GetInstance().Notify(message);
        break;
    }
  }
};

/** Return true iff host is an IPv4 multicast address (224.0.0.0/4). */
static bool IsMulticastAddr(const std::string& host) {
  const int first = atoi(host.c_str());
  return first >= 224 && first <= 239;
}

/**
 * Build an NMEA 0183 network driver.
 *
 * TCP-client and UDP connections run on the P1.5i comms framework -- a
 * generic CommDriver wrapping a transport + LineFramer + Nmea0183Decoder.
 *
 * TCP server-mode (a 0.0.0.0 listen address) and GPSD are out of scope:
 * they are treated as edge cases and produce no driver. The legacy
 * CommDriverN0183Net implementation is kept in the tree but is no longer
 * reachable from the factory. See QT_MIGRATION_TASKS.md (P1.5b follow-up)
 * for the review note -- restoring them means a TcpServerTransport and a
 * GPSD handshake option at the transport layer.
 */
static DriverPtr MakeN0183NetDriver(const ConnectionParams* params,
                                    DriverListener& listener) {
  const std::string host = params->NetworkAddress.ToStdString();
  const bool is_server = host.empty() || host == "0.0.0.0";
  const auto port = static_cast<quint16>(params->NetworkPort);

  std::unique_ptr<CommTransport> transport;
  if (params->NetProtocol == TCP && !is_server)
    transport =
        std::make_unique<TcpClientTransport>(QString::fromStdString(host), port);
  else if (params->NetProtocol == UDP)
    transport = std::make_unique<UdpTransport>(QString::fromStdString(host),
                                               port, IsMulticastAddr(host));
  else {
    wxLogMessage(
        "MakeCommDriver: NMEA 0183 network %s mode is out of scope -- "
        "no driver created for %s",
        params->NetProtocol == GPSD ? "GPSD" : "TCP server",
        params->GetDSPort().c_str());
    return nullptr;
  }

  auto driver = std::make_unique<CommDriver>(
      NavAddr::Bus::N0183, params->GetStrippedDSPort(), *params,
      std::move(transport), std::make_unique<LineFramer>(),
      std::make_unique<Nmea0183Decoder>(*params), listener);
  driver->attributes["netAddress"] = host;
  driver->attributes["netPort"] = std::to_string(params->NetworkPort);
  driver->attributes["userComment"] = params->UserComment.ToStdString();
  driver->attributes["ioDirection"] = DsPortTypeToString(params->IOSelect);
  return driver;
}

/**
 * Build an NMEA 2000 serial-gateway driver on the comms framework -- a
 * generic CommDriver wrapping a SerialTransport + N2kGatewayFramer +
 * N2kDecoder, with an N2kGatewayManager attached for the async
 * NGT-1 / YDNU-02 management handshake.
 */
static DriverPtr MakeN2kSerialDriver(const ConnectionParams* params,
                                     DriverListener& listener) {
  // Strip the "Serial:" prefix and any trailing device description.
  std::string dsport = params->GetDSPort().ToStdString();
  const auto colon = dsport.find(':');
  std::string port =
      colon == std::string::npos ? dsport : dsport.substr(colon + 1);
  const auto space = port.find(' ');
  if (space != std::string::npos) port.resize(space);

  auto transport = std::make_unique<SerialTransport>(
      QString::fromStdString(port), static_cast<qint32>(params->Baudrate));
  auto driver = std::make_unique<CommDriver>(
      NavAddr::Bus::N2000, params->GetStrippedDSPort(), *params,
      std::move(transport), std::make_unique<N2kGatewayFramer>(),
      std::make_unique<N2kDecoder>(*params), listener);
  driver->attributes["canAddress"] = std::string("-1");
  driver->attributes["userComment"] = params->UserComment.ToStdString();
  driver->attributes["ioDirection"] = DsPortTypeToString(params->IOSelect);

  // The gateway manager QObject-parents itself to the driver, so it lives
  // and dies with it; it wires itself onto the driver in its constructor.
  new N2kGatewayManager(*driver, driver.get());
  return driver;
}

/**
 * Build an NMEA 0183 serial driver on the comms framework -- a generic
 * CommDriver wrapping a SerialTransport + LineFramer + Nmea0183Decoder.
 */
static DriverPtr MakeN0183SerialDriver(const ConnectionParams* params,
                                       DriverListener& listener) {
  // Strip the "Serial:" prefix and any trailing device description.
  std::string dsport = params->GetDSPort().ToStdString();
  const auto colon = dsport.find(':');
  std::string port =
      colon == std::string::npos ? dsport : dsport.substr(colon + 1);
  const auto space = port.find(' ');
  if (space != std::string::npos) port.resize(space);

  auto transport = std::make_unique<SerialTransport>(
      QString::fromStdString(port), static_cast<qint32>(params->Baudrate));
  auto driver = std::make_unique<CommDriver>(
      NavAddr::Bus::N0183, params->GetStrippedDSPort(), *params,
      std::move(transport), std::make_unique<LineFramer>(),
      std::make_unique<Nmea0183Decoder>(*params), listener);
  driver->attributes["commPort"] = params->Port.ToStdString();
  driver->attributes["userComment"] = params->UserComment.ToStdString();
  driver->attributes["ioDirection"] = DsPortTypeToString(params->IOSelect);
  return driver;
}

void MakeLoopbackDriver() {
  auto driver = std::make_unique<LoopbackDriver>(NavMsgBus::GetInstance());
  CommDriverRegistry::GetInstance().Activate(std::move(driver));
}

void MakeCommDriver(const ConnectionParams* params) {
  static N0183Listener listener;

  wxLogMessage("MakeCommDriver: %s", params->GetDSPort().c_str());

  auto& msgbus = NavMsgBus::GetInstance();
  auto& registry = CommDriverRegistry::GetInstance();
  switch (params->Type) {
    case SERIAL:
      switch (params->Protocol) {
        case PROTO_NMEA2000: {
          registry.Activate(MakeN2kSerialDriver(params, msgbus));
          break;
        }
        default: {
          registry.Activate(MakeN0183SerialDriver(params, listener));
          break;
        }
      }
      break;
    case NETWORK:
      switch (params->NetProtocol) {
        case SIGNALK: {
          // Parked -- the SignalK driver is not built. See P1.5m.
          wxLogMessage(
              "MakeCommDriver: SignalK is out of scope -- no driver "
              "created for %s",
              params->GetDSPort().c_str());
          break;
        }
        default: {
          switch (params->Protocol) {
            case PROTO_NMEA0183: {
              if (auto driver = MakeN0183NetDriver(params, listener))
                registry.Activate(std::move(driver));
              break;
            }
            case PROTO_NMEA2000: {
              auto driver = std::make_unique<CommDriverN2KNet>(params, msgbus);
              registry.Activate(std::move(driver));
              break;
            }
            default:
              break;
          }
          break;
        }
      }

      break;

    case SOCKETCAN:
      // Parked -- the SocketCAN driver is not built. See P1.5g / P1.5m.
      wxLogMessage("MakeCommDriver: SocketCAN is out of scope -- no driver");
      break;

    default:
      break;
  }
};

// IXWebSocket network-system init. Formerly delegated to the SignalK
// driver (now parked); kept functional here directly so a future
// IXWebSocket consumer -- or the revived SignalK driver -- still works.
void initIXNetSystem() { ix::initNetSystem(); };

void uninitIXNetSystem() { ix::uninitNetSystem(); };
