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

#include "model/comm_util.h"
#include "model/comm_drv_generic.h"
#include "model/comm_drv_loopback.h"
#include "model/comm_drv_n2k_net.h"
#include "model/comm_drv_n2k_serial.h"
#include "model/comm_drv_n0183_serial.h"
#include "model/comm_drv_n0183_net.h"
#include "model/comm_drv_signalk_net.h"
#include "model/comm_n0183_decoder.h"
#include "model/comm_navmsg_bus.h"
#include "model/comm_drv_registry.h"
#include "model/ds_porttype.h"

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__WXOSX__)
#include "model/comm_drv_n2k_socketcan.h"
#endif

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
 * TCP server-mode (a 0.0.0.0 listen address) and GPSD stay on the legacy
 * CommDriverN0183Net until a TcpServerTransport / GPSD transport option
 * lands (P1.5b follow-up).
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
  else
    return std::make_unique<CommDriverN0183Net>(params, listener);

  auto driver = std::make_unique<CommDriver>(
      NavAddr::Bus::N0183, params->GetStrippedDSPort(), std::move(transport),
      std::make_unique<LineFramer>(), std::make_unique<Nmea0183Decoder>(*params),
      listener);
  driver->attributes["netAddress"] = host;
  driver->attributes["netPort"] = std::to_string(params->NetworkPort);
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
          auto driver = std::make_unique<CommDriverN2KSerial>(params, msgbus);
          registry.Activate(std::move(driver));
          break;
        }
        default: {
          auto driver =
              std::make_unique<CommDriverN0183Serial>(params, listener);
          registry.Activate(std::move(driver));
          break;
        }
      }
      break;
    case NETWORK:
      switch (params->NetProtocol) {
        case SIGNALK: {
          auto driver = std::make_unique<CommDriverSignalKNet>(params, msgbus);
          registry.Activate(std::move(driver));
          break;
        }
        default: {
          switch (params->Protocol) {
            case PROTO_NMEA0183: {
              registry.Activate(MakeN0183NetDriver(params, listener));
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
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__WXOSX__)
    case SOCKETCAN: {
      auto driver = CommDriverN2KSocketCAN::Create(params, msgbus);
      registry.Activate(std::move(driver));
      break;
    }
#endif

    default:
      break;
  }
};

void initIXNetSystem() { CommDriverSignalKNet::initIXNetSystem(); };

void uninitIXNetSystem() { CommDriverSignalKNet::uninitIXNetSystem(); };
