/***************************************************************************
 *   Copyright (C) 2010 by David S. Register                               *
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
 * NMEA Data Multiplexer Object
 */

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "config.h"

#include <wx/jsonreader.h>
#include <wx/jsonval.h>
#include <wx/jsonwriter.h>
#include <wx/tokenzr.h>

#include "model/comm_driver.h"
#include "model/comm_drv_factory.h"
#include "model/comm_drv_n0183_net.h"
#include "model/comm_drv_registry.h"
#include "model/comm_n0183_output.h"
#include "model/config_vars.h"
#include "model/conn_params.h"
#include "model/logger.h"
#include "model/nmea_ctx_factory.h"
#include "model/nmea_log.h"
#include "model/route.h"
#include "nmea0183.h"

#ifdef USE_GARMINHOST
#include "model/garmin_wrapper.h"
#endif

void LogBroadcastOutputMessageColor(const std::shared_ptr<const NavMsg>& msg,
                                    NavmsgStatus ns, NmeaLog* nmea_log) {
  if (nmea_log->IsVisible()) {
    ns.direction = NavmsgStatus::Direction::kOutput;
    Logline ll(msg, ns);
    nmea_log->Add(ll);
  }
}

void BroadcastNMEA0183Message(const wxString& msg, NmeaLog* nmea_log,
                              EventVar& on_msg_sent) {
  auto& registry = CommDriverRegistry::GetInstance();
  const std::vector<std::unique_ptr<AbstractCommDriver>>& drivers =
      registry.GetDrivers();

  for (auto& driver : drivers) {
    if (driver->bus == NavAddr::Bus::N0183) {
      // Any driver carrying connection params works here, whatever its
      // concrete class -- framework CommDriver or a legacy driver.
      auto* cpp = dynamic_cast<ConnectionParamsProvider*>(driver.get());
      if (!cpp) continue;
      const ConnectionParams& params = cpp->GetConnectionParams();

      if (params.IOSelect == DS_TYPE_INPUT_OUTPUT ||
          params.IOSelect == DS_TYPE_OUTPUT) {
        std::string id = msg.ToStdString().substr(1, 5);
        auto source_addr =
            std::make_shared<NavAddr>(NavAddr0183(params.GetStrippedDSPort()));
        auto msg_out =
            std::make_shared<Nmea0183Msg>(id, msg.ToStdString(), source_addr);
        NavmsgStatus ns;
        ns.direction = NavmsgStatus::Direction::kOutput;
        if (params.SentencePassesFilter(msg, FILTER_OUTPUT)) {
          bool xmit_ok = driver->SendMessage(msg_out, source_addr);
          if (!xmit_ok) ns.status = NavmsgStatus::State::kTxError;
        } else {
          ns.accepted = NavmsgStatus::Accepted::kFilteredDropped;
        }
        LogBroadcastOutputMessageColor(msg_out, ns, nmea_log);
      }
    }
  }
  // Send to plugins
  on_msg_sent.Notify(msg.ToStdString());
}

static wxString qs2ws(const QString& qs) {
  return wxString::FromUTF8(qs.toStdString());
}

bool CreateOutputConnection(const wxString& com_name,
                            ConnectionParams& params_save, bool& btempStream,
                            bool& b_restoreStream, N0183DlgCtx dlg_ctx,
                            bool bGarminIn) {
  AbstractCommDriver* driver(nullptr);
  auto& registry = CommDriverRegistry::GetInstance();
  const std::vector<DriverPtr>& drivers = registry.GetDrivers();

  int baud = 0;
  wxString comx;
  bool bGarmin = false;
  if (com_name.Lower().StartsWith("serial")) {
    comx = com_name.AfterFirst(':');  // strip "Serial:"
    comx =
        comx.BeforeFirst(' ');  // strip off any description provided by Windows
    DriverPtr& old_driver = FindDriver(drivers, comx.ToStdString());
    wxLogDebug("Looking for old stream %s", com_name);

    if (old_driver) {
      // Remember the existing connection's settings so it can be restored
      // after the temporary output stream is done.
      if (auto* cpp =
              dynamic_cast<ConnectionParamsProvider*>(old_driver.get())) {
        params_save = cpp->GetConnectionParams();
        baud = params_save.Baudrate;
      }
      // Deactivate destroys the driver, synchronously closing its port, so
      // the temporary output driver below can claim it.
      registry.Deactivate(old_driver);

      b_restoreStream = true;
    }

    if (baud == 0) baud = 4800;
  }
  if (com_name.Lower().StartsWith("serial")) {
    ConnectionParams cp;
    cp.Type = SERIAL;
    cp.SetPortStr(comx);
    cp.Baudrate = baud;
    cp.Garmin = bGarminIn || bGarmin;
    cp.IOSelect = DS_TYPE_OUTPUT;

    MakeCommDriver(&cp);
    btempStream = true;

#ifdef __ANDROID__
    wxMilliSleep(1000);
#else
    // The framework serial driver opens its port synchronously in its
    // constructor, so the temporary output driver is ready as soon as
    // MakeCommDriver() returns -- no need to poll a worker thread.
    driver = FindDriver(drivers, comx.ToStdString(), NavAddr::Bus::N0183).get();
#endif
  }

  if (com_name.Find("Bluetooth") != wxNOT_FOUND) {
    wxString comm_addr = com_name.AfterFirst(';');

    driver = FindDriver(drivers, comm_addr.ToStdString()).get();
    if (!driver) {
      // Force Android Bluetooth to use only already enabled driver
      return false;
    }
  } else if (com_name.Lower().StartsWith("udp") ||
             com_name.Lower().StartsWith("tcp")) {
    CommDriverN0183Net* drv_net_n0183(nullptr);
    driver =
        FindDriver(drivers, com_name.ToStdString(), NavAddr::Bus::N0183).get();

    if (!driver) {
      NetworkProtocol protocol = UDP;
      if (com_name.Lower().StartsWith("tcp")) protocol = TCP;
      wxStringTokenizer tkz(com_name, ":");
      wxString token = tkz.GetNextToken();
      wxString address = tkz.GetNextToken();
      token = tkz.GetNextToken();
      long port;
      token.ToLong(&port);

      ConnectionParams cp;
      cp.Type = NETWORK;
      cp.NetProtocol = protocol;
      cp.NetworkAddress = address;
      cp.NetworkPort = port;
      cp.IOSelect = DS_TYPE_OUTPUT;  // DS_TYPE_INPUT_OUTPUT;

      MakeCommDriver(&cp);
      auto& me =
          FindDriver(drivers, cp.GetStrippedDSPort(), cp.GetLastCommProtocol());
      driver = me.get();
      btempStream = true;
    }
    drv_net_n0183 = dynamic_cast<CommDriverN0183Net*>(driver);

    if (com_name.Lower().StartsWith("tcp")) {
      // new tcp connections must wait for connect
      std::string msg(_("Connecting to "));
      msg += com_name;
      dlg_ctx.set_message(msg);
      dlg_ctx.pulse();

      if (drv_net_n0183) {
        int loopCount = 10;  // seconds
        bool bconnected;
        for (bconnected = false; !bconnected && (loopCount > 0); loopCount--) {
          if (drv_net_n0183->GetSock()->IsConnected()) {
            bconnected = true;
            break;
          }
          dlg_ctx.pulse();
          wxYield();
          wxSleep(1);
        }
        if (bconnected) {
          msg = _("Connected to ");
          msg += com_name;
          dlg_ctx.set_message(msg);
        } else {
          if (btempStream) {
            auto& me = FindDriver(drivers, driver->iface, driver->bus);
            registry.Deactivate(me);
          }
          return 0;
        }
      }
    }
  }
  return driver != nullptr;
}

int PrepareOutputChannel(const wxString& com_name, N0183DlgCtx dlg_ctx,
                         ConnectionParams& params_save, bool& b_restoreStream,
                         bool& btempStream) {
  int ret_val = 0;
  auto& registry = CommDriverRegistry::GetInstance();

  bool conn_ok = CreateOutputConnection(com_name, params_save, btempStream,
                                        b_restoreStream, dlg_ctx, false);
  if (!conn_ok) return 1;

  return ret_val;
}
int SendRouteToGPS_N0183(Route* pr, const wxString& com_name,
                         bool bsend_waypoints, Multiplexer& multiplexer,
                         N0183DlgCtx dlg_ctx) {
  int ret_val = 0;

  ConnectionParams params_save;
  bool b_restoreStream = false;
  bool btempStream = false;

  auto& registry = CommDriverRegistry::GetInstance();

  PrepareOutputChannel(com_name, dlg_ctx, params_save, b_restoreStream,
                       btempStream);

  std::string target_iface =
      com_name.AfterFirst(':').ToStdString();  // For serial ports
  if (com_name.Lower().StartsWith("udp") || com_name.Lower().StartsWith("tcp"))
    target_iface = com_name.ToStdString();  // for network

  auto& target_driver =
      FindDriver(registry.GetDrivers(), target_iface, NavAddr::Bus::N0183);

  // Only iface / SendMessage are needed below -- both AbstractCommDriver
  // members -- so no concrete-driver downcast is required.
  AbstractCommDriver* drv_n0183 = target_driver.get();
  if (!drv_n0183) {
    return ERR_GPS_DRIVER_NOT_AVAILAIBLE;
  }

#ifdef USE_GARMINHOST
#ifdef __WXMSW__
  if (com_name.Upper().Matches("*GARMIN*"))  // Garmin USB Mode
  {
    // Release the port so the Garmin host library can open it directly.
    if (target_driver) registry.Deactivate(target_driver);

    {
      int v_init = Garmin_GPS_Init(wxString("usb:"));
      if (v_init < 0) {
        MESSAGE_LOG << "Garmin USB GPS could not be initialized, error code: "
                    << v_init << " LastGarminError: " << GetLastGarminError();
        ret_val = ERR_GARMIN_INITIALIZE;
      } else {
        MESSAGE_LOG << "Garmin USB initialized, unit identifies as "
                    << Garmin_GPS_GetSaveString();
      }

      wxLogMessage("Sending Routes...");
      int ret1 = Garmin_GPS_SendRoute(wxString("usb:"), pr, dlg_ctx);

      if (ret1 != 1) {
        MESSAGE_LOG << " Error sending routes, last garmin error: "
                    << GetLastGarminError();
        ret_val = ERR_GARMIN_SEND_MESSAGE;
      } else
        ret_val = 0;
    }

    goto ret_point_1;
  }
#endif
#endif

  if (g_bGarminHostUpload) {
    //  Close and abandon the tentatively opened target_driver
    // Release the port so the Garmin host library can open it directly.
    if (target_driver) registry.Deactivate(target_driver);

    int lret_val;
    dlg_ctx.set_value(20);

    wxString short_com = com_name.Mid(7);
    // Initialize the Garmin receiver, build required Jeeps internal data
    // structures
    // Retry 5 times, 1 sec cycle
    int n_try = 5;
    int v_init = 0;
    while (n_try) {
      v_init = Garmin_GPS_Init(short_com);
      if (v_init >= 0) break;
      n_try--;
      wxMilliSleep(1000);
    }
    if (v_init < 0) {
      MESSAGE_LOG << "Garmin GPS could not be initialized on port: "
                  << short_com << " Error Code: " << v_init
                  << " LastGarminError: " << GetLastGarminError();

      ret_val = ERR_GARMIN_INITIALIZE;
      goto ret_point;
    } else {
      MESSAGE_LOG << "Sendig Route to Garmin GPS on port: " << short_com
                  << "Unit identifies as: " << Garmin_GPS_GetSaveString();
    }

    dlg_ctx.set_value(40);
    lret_val = Garmin_GPS_SendRoute(short_com, pr, dlg_ctx);
    if (lret_val != 1) {
      MESSAGE_LOG << "Error Sending Route to Garmin GPS on port: " << short_com
                  << " Error Code: " << lret_val
                  << " LastGarminError: " << GetLastGarminError();
      ret_val = ERR_GARMIN_SEND_MESSAGE;
      goto ret_point;
    } else {
      ret_val = 0;
    }

  ret_point:

    dlg_ctx.set_value(100);

    wxMilliSleep(500);

    goto ret_point_1;
  } else

  {
    auto address =
        std::make_shared<NavAddr>(NavAddr::Bus::N0183, drv_n0183->iface);
    SENTENCE snt;
    NMEA0183 oNMEA0183(NmeaCtxFactory());
    oNMEA0183.TalkerID = _T ( "EC" );

    int nProg = pr->pRoutePointList->size() + 1;
    dlg_ctx.set_range(100);

    int progress_stall = 500;
    if (pr->pRoutePointList->size() > 10) progress_stall = 200;

    //       if (!dialog) progress_stall = 200;  // 80 chars at 4800 baud is
    //       ~160 msec

    // Send out the waypoints, in order
    if (bsend_waypoints) {
      int ip = 1;
      for (RoutePoint* prp : *pr->pRoutePointList) {
        if (g_GPS_Ident == "Generic") {
          if (prp->m_lat < 0.)
            oNMEA0183.Wpl.Position.Latitude.Set(-prp->m_lat, _T ( "S" ));
          else
            oNMEA0183.Wpl.Position.Latitude.Set(prp->m_lat, _T ( "N" ));

          if (prp->m_lon < 0.)
            oNMEA0183.Wpl.Position.Longitude.Set(-prp->m_lon, _T ( "W" ));
          else
            oNMEA0183.Wpl.Position.Longitude.Set(prp->m_lon, _T ( "E" ));

          oNMEA0183.Wpl.To = qs2ws(prp->GetName().left(g_maxWPNameLength));

          oNMEA0183.Wpl.Write(snt);

        } else if (g_GPS_Ident == "FurunoGP3X") {
          //  Furuno has its own talker ID, so do not allow the global
          //  override
          wxString talker_save = g_TalkerIdText;
          g_TalkerIdText.Clear();

          oNMEA0183.TalkerID = _T ( "PFEC," );

          if (prp->m_lat < 0.)
            oNMEA0183.GPwpl.Position.Latitude.Set(-prp->m_lat, _T ( "S" ));
          else
            oNMEA0183.GPwpl.Position.Latitude.Set(prp->m_lat, _T ( "N" ));

          if (prp->m_lon < 0.)
            oNMEA0183.GPwpl.Position.Longitude.Set(-prp->m_lon, _T ( "W" ));
          else
            oNMEA0183.GPwpl.Position.Longitude.Set(prp->m_lon, _T ( "E" ));

          wxString name = qs2ws(prp->GetName());
          name += "000000";
          name.Truncate(g_maxWPNameLength);
          oNMEA0183.GPwpl.To = name;

          oNMEA0183.GPwpl.Write(snt);

          g_TalkerIdText = talker_save;
        }

        wxString payload = snt.Sentence;

        // for some gps, like some garmin models, they assume the first
        // waypoint in the route is the boat location, therefore it is
        // dropped. These gps also can only accept a maximum of up to 20
        // waypoints at a time before a delay is needed and a new string of
        // waypoints may be sent. To ensure all waypoints will arrive, we can
        // simply send each one twice. This ensures that the gps  will get the
        // waypoint and also allows us to send as many as we like
        //
        //  We need only send once for FurunoGP3X models

        auto msg_out = std::make_shared<Nmea0183Msg>(
            "ECWPL", snt.Sentence.ToStdString(), address);

        drv_n0183->SendMessage(msg_out, std::make_shared<NavAddr>());
        if (g_GPS_Ident != "FurunoGP3X")
          drv_n0183->SendMessage(msg_out, std::make_shared<NavAddr>());

        NavmsgStatus ns;
        ns.direction = NavmsgStatus::Direction::kOutput;
        multiplexer.LogOutputMessage(msg_out, ns);
        auto msg =
            wxString("-->GPS Port: ") + com_name + " Sentence: " + snt.Sentence;
        msg.Trim();
        wxLogMessage(msg);

        dlg_ctx.set_value((ip * 100) / nProg);

        wxMilliSleep(progress_stall);
        ip++;
      }
    }

    // Create the NMEA Rte sentence
    // Try to create a single sentence, and then check the length to see if
    // too long
    unsigned int max_length = 76;
    unsigned int max_wp = 2;  // seems to be required for garmin...

    //  Furuno GPS can only accept 5 (five) waypoint linkage sentences....
    //  So, we need to compact a few more points into each link sentence.
    if (g_GPS_Ident == "FurunoGP3X") {
      max_wp = 8;
      max_length = 80;
    }

    //  Furuno has its own talker ID, so do not allow the global override
    wxString talker_save = g_TalkerIdText;
    if (g_GPS_Ident == "FurunoGP3X") g_TalkerIdText.Clear();

    oNMEA0183.Rte.Empty();
    oNMEA0183.Rte.TypeOfRoute = CompleteRoute;

    if (pr->m_RouteNameString.isEmpty())
      oNMEA0183.Rte.RouteName = _T ( "1" );
    else
      oNMEA0183.Rte.RouteName = qs2ws(pr->m_RouteNameString);

    if (g_GPS_Ident == "FurunoGP3X") {
      oNMEA0183.Rte.RouteName = _T ( "01" );
      oNMEA0183.TalkerID = _T ( "GP" );
      oNMEA0183.Rte.m_complete_char = 'C';  // override the default "c"
      oNMEA0183.Rte.m_skip_checksum = 1;    // no checksum needed
    }

    oNMEA0183.Rte.total_number_of_messages = 1;
    oNMEA0183.Rte.message_number = 1;

    // add the waypoints
    for (RoutePoint* prp : *pr->pRoutePointList) {
      wxString name = qs2ws(prp->GetName().left(g_maxWPNameLength));
      if (g_GPS_Ident == "FurunoGP3X") {
        name = qs2ws(prp->GetName());
        name += "000000";
        name.Truncate(g_maxWPNameLength);
        name.Prepend(" ");  // What Furuno calls "Skip Code", space means
                            // use the WP
      }
      oNMEA0183.Rte.AddWaypoint(name);
    }

    oNMEA0183.Rte.Write(snt);

    if ((snt.Sentence.Len() > max_length) ||
        (pr->pRoutePointList->size() > max_wp))  // Do we need split sentences?
    {
      // Make a route with zero waypoints to get tare load.
      NMEA0183 tNMEA0183(NmeaCtxFactory());
      SENTENCE tsnt;
      tNMEA0183.TalkerID = _T ( "EC" );

      tNMEA0183.Rte.Empty();
      tNMEA0183.Rte.TypeOfRoute = CompleteRoute;

      if (g_GPS_Ident != "FurunoGP3X") {
        if (pr->m_RouteNameString.isEmpty())
          tNMEA0183.Rte.RouteName = _T ( "1" );
        else
          tNMEA0183.Rte.RouteName = qs2ws(pr->m_RouteNameString);

      } else {
        tNMEA0183.Rte.RouteName = _T ( "01" );
      }

      tNMEA0183.Rte.Write(tsnt);

      unsigned int tare_length = tsnt.Sentence.Len();
      tare_length -= 3;  // Drop the checksum, for length calculations

      wxArrayString sentence_array;

      // Trial balloon: add the waypoints, with length checking
      int n_total = 1;
      bool bnew_sentence = true;
      int sent_len = 0;
      unsigned int wp_count = 0;

      auto _node = pr->pRoutePointList->begin();
      while (_node != pr->pRoutePointList->end()) {
        RoutePoint* prp = *_node;
        unsigned int name_len =
            qs2ws(prp->GetName().left(g_maxWPNameLength)).Len();
        if (g_GPS_Ident == "FurunoGP3X")
          name_len = 7;  // six chars, with leading space for "Skip Code"

        if (bnew_sentence) {
          sent_len = tare_length;
          sent_len += name_len + 1;  // with comma
          bnew_sentence = false;
          ++_node;
          wp_count = 1;

        } else {
          if ((sent_len + name_len > max_length) || (wp_count >= max_wp)) {
            n_total++;
            bnew_sentence = true;
          } else {
            if (wp_count == max_wp)
              sent_len += name_len;  // with comma
            else
              sent_len += name_len + 1;  // with comma
            wp_count++;
            ++_node;
          }
        }
      }

      // Now we have the sentence count, so make the real sentences using the
      // same counting logic
      int final_total = n_total;
      int n_run = 1;
      bnew_sentence = true;

      auto it = pr->pRoutePointList->begin();
      while (it != pr->pRoutePointList->end()) {
        RoutePoint* prp = *it;
        wxString name = qs2ws(prp->GetName().left(g_maxWPNameLength));
        if (g_GPS_Ident == "FurunoGP3X") {
          name = qs2ws(prp->GetName());
          name += "000000";
          name.Truncate(g_maxWPNameLength);
          name.Prepend(" ");  // What Furuno calls "Skip Code", space
                              // means use the WP
        }

        unsigned int name_len = name.Len();

        if (bnew_sentence) {
          sent_len = tare_length;
          sent_len += name_len + 1;  // comma
          bnew_sentence = false;

          oNMEA0183.Rte.Empty();
          oNMEA0183.Rte.TypeOfRoute = CompleteRoute;

          if (g_GPS_Ident != "FurunoGP3X") {
            if (pr->m_RouteNameString.isEmpty())
              oNMEA0183.Rte.RouteName = "1";
            else
              oNMEA0183.Rte.RouteName = qs2ws(pr->m_RouteNameString);
          } else {
            oNMEA0183.Rte.RouteName = "01";
          }

          oNMEA0183.Rte.total_number_of_messages = final_total;
          oNMEA0183.Rte.message_number = n_run;
          snt.Sentence.Clear();
          wp_count = 1;

          oNMEA0183.Rte.AddWaypoint(name);
          ++it;
        } else {
          if ((sent_len + name_len > max_length) || (wp_count >= max_wp)) {
            n_run++;
            bnew_sentence = true;

            oNMEA0183.Rte.Write(snt);

            sentence_array.Add(snt.Sentence);
          } else {
            sent_len += name_len + 1;  // comma
            oNMEA0183.Rte.AddWaypoint(name);
            wp_count++;
            ++it;
          }
        }
      }

      oNMEA0183.Rte.Write(snt);  // last one...
      if (snt.Sentence.Len() > tare_length) sentence_array.Add(snt.Sentence);

      for (unsigned int ii = 0; ii < sentence_array.GetCount(); ii++) {
        wxString sentence = sentence_array[ii];

        auto msg_out = std::make_shared<Nmea0183Msg>(
            "ECRTE", sentence.ToStdString(), std::make_shared<NavAddr>());
        drv_n0183->SendMessage(msg_out, address);

        NavmsgStatus ns;
        ns.direction = NavmsgStatus::Direction::kOutput;
        multiplexer.LogOutputMessage(msg_out, ns);
        wxYield();

        //             LogOutputMessage(sentence, dstr->GetPort(), false);
        auto msg =
            wxString("-->GPS Port: ") + com_name + " Sentence: " + sentence;
        msg.Trim();
        wxLogMessage(msg);

        wxMilliSleep(progress_stall);
      }

    } else {
      auto msg_out = std::make_shared<Nmea0183Msg>(
          "ECRTE", snt.Sentence.ToStdString(), address);
      drv_n0183->SendMessage(msg_out, address);

      NavmsgStatus ns;
      ns.direction = NavmsgStatus::Direction::kOutput;
      multiplexer.LogOutputMessage(msg_out, ns);
      wxYield();

      auto msg =
          wxString("-->GPS Port:") + com_name + " Sentence: " + snt.Sentence;
      msg.Trim();
      wxLogMessage(msg);
    }

    if (g_GPS_Ident == "FurunoGP3X") {
      wxString name = qs2ws(pr->GetName());
      if (name.IsEmpty()) name = "RTECOMMENT";
      wxString rte;
      rte.Printf("$PFEC,GPrtc,01,");
      rte += name.Left(16);
      wxString rtep;
      rtep.Printf(",%c%c", 0x0d, 0x0a);
      rte += rtep;

      auto msg_out =
          std::make_shared<Nmea0183Msg>("GPRTC", rte.ToStdString(), address);
      drv_n0183->SendMessage(msg_out, address);
      NavmsgStatus ns;
      ns.direction = NavmsgStatus::Direction::kOutput;
      multiplexer.LogOutputMessage(msg_out, ns);

      auto msg = wxString("-->GPS Port:") + com_name + " Sentence: " + rte;
      msg.Trim();
      wxLogMessage(msg);

      wxString term;
      term.Printf("$PFEC,GPxfr,CTL,E%c%c", 0x0d, 0x0a);

      auto msg_outf =
          std::make_shared<Nmea0183Msg>("GPRTC", term.ToStdString(), address);
      drv_n0183->SendMessage(msg_outf, address);

      ns = NavmsgStatus();
      ns.direction = NavmsgStatus::Direction::kOutput;
      multiplexer.LogOutputMessage(msg_outf, ns);

      msg = wxString("-->GPS Port:") + com_name + " Sentence: " + term;
      msg.Trim();
      wxLogMessage(msg);
    }
    dlg_ctx.set_value(100);

    wxMilliSleep(progress_stall);

    ret_val = 0;

    if (g_GPS_Ident == "FurunoGP3X") g_TalkerIdText = talker_save;
  }
ret_point_1:
  //  All finished with the temp port
  if (btempStream) {
    registry.Deactivate(target_driver);
  }

  if (b_restoreStream) {
    wxMilliSleep(500);  // Give temp driver a chance to die
    MakeCommDriver(&params_save);
  }

  return ret_val;
}

int SendWaypointToGPS_N0183(RoutePoint* prp, const wxString& com_name,
                            Multiplexer& multiplexer, N0183DlgCtx dlg_ctx) {
  int ret_val = 0;

  ConnectionParams params_save;
  bool b_restoreStream = false;
  bool btempStream = false;

  auto& registry = CommDriverRegistry::GetInstance();

  PrepareOutputChannel(com_name, dlg_ctx, params_save, b_restoreStream,
                       btempStream);

  auto& target_driver =
      FindDriver(registry.GetDrivers(), com_name.AfterFirst(':').ToStdString(),
                 NavAddr::Bus::N0183);

#ifdef USE_GARMINHOST
#ifdef __WXMSW__
  if (com_name.Upper().Matches("*GARMIN*"))  // Garmin USB Mode
  {
    // Release the port so the Garmin host library can open it directly.
    if (target_driver) registry.Deactivate(target_driver);

    {
      int v_init = Garmin_GPS_Init(wxString("usb:"));
      if (v_init < 0) {
        MESSAGE_LOG << "Garmin USB GPS could not be initialized, last error: "
                    << v_init << " LastGarminError: " << GetLastGarminError();

        ret_val = ERR_GARMIN_INITIALIZE;
      } else {
        MESSAGE_LOG << "Garmin USB Initialized, unit identifies as: "
                    << Garmin_GPS_GetSaveString();
      }
    }
    wxLogMessage("Sending Waypoint...");

    // Create a RoutePointList with one item
    RoutePointList rplist;
    rplist.push_back(prp);

    int ret1 = Garmin_GPS_SendWaypoints(wxString("usb:"), &rplist);

    if (ret1 != 1) {
      MESSAGE_LOG << "Error Sending Waypoint to Garmin USB, last error: "
                  << GetLastGarminError();

      ret_val = ERR_GARMIN_SEND_MESSAGE;
    } else
      ret_val = 0;

    goto ret_point;
  }

#endif
#endif

#ifdef USE_GARMINHOST
  // Are we using Garmin Host mode for uploads?
  if (g_bGarminHostUpload) {
    //  Close and abandon the tentatively opened target_driver
    // Release the port so the Garmin host library can open it directly.
    if (target_driver) registry.Deactivate(target_driver);
    RoutePointList rplist;

    wxString short_com = com_name.Mid(7);
    // Initialize the Garmin receiver, build required Jeeps internal data
    // structures
    // Retry 5 times, 1 sec cycle
    int n_try = 5;
    int v_init = 0;
    while (n_try) {
      v_init = Garmin_GPS_Init(short_com);
      if (v_init >= 0) break;
      n_try--;
      wxMilliSleep(1000);
    }
    if (v_init < 0) {
      MESSAGE_LOG << "Garmin GPS could not be initialized on port: " << com_name
                  << " Error Code: " << v_init
                  << "LastGarminError: " << GetLastGarminError();

      ret_val = ERR_GARMIN_INITIALIZE;
      goto ret_point;
    } else {
      MESSAGE_LOG << "Sending waypoint(s) to Garmin GPS on port: " << com_name;
      MESSAGE_LOG << "Unit identifies as: " << Garmin_GPS_GetSaveString();
    }

    // Create a RoutePointList with one item
    rplist.push_back(prp);

    ret_val = Garmin_GPS_SendWaypoints(short_com, &rplist);
    if (ret_val != 1) {
      MESSAGE_LOG << "Error Sending Waypoint(s) to Garmin GPS on port, "
                  << com_name << " error code: " << ret_val
                  << ", last garmin error: " << GetLastGarminError();
      ret_val = ERR_GARMIN_SEND_MESSAGE;
      goto ret_point;
    } else
      ret_val = 0;

    goto ret_point;
  } else
#endif  // USE_GARMINHOST

  {  // Standard NMEA mode
    AbstractCommDriver* drv_n0183 = target_driver.get();
    if (!drv_n0183) {
      ret_val = ERR_GPS_DRIVER_NOT_AVAILAIBLE;
      goto ret_point;
    }

    auto address = std::make_shared<NavAddr>();
    SENTENCE snt;
    NMEA0183 oNMEA0183(NmeaCtxFactory());
    oNMEA0183.TalkerID = "EC";
    dlg_ctx.set_range(100);

    if (g_GPS_Ident == "Generic") {
      if (prp->m_lat < 0.)
        oNMEA0183.Wpl.Position.Latitude.Set(-prp->m_lat, "S");
      else
        oNMEA0183.Wpl.Position.Latitude.Set(prp->m_lat, "N");

      if (prp->m_lon < 0.)
        oNMEA0183.Wpl.Position.Longitude.Set(-prp->m_lon, "W");
      else
        oNMEA0183.Wpl.Position.Longitude.Set(prp->m_lon, "E");

      oNMEA0183.Wpl.To = qs2ws(prp->GetName().left(g_maxWPNameLength));

      oNMEA0183.Wpl.Write(snt);
    } else if (g_GPS_Ident == "FurunoGP3X") {
      oNMEA0183.TalkerID = "PFEC,";

      if (prp->m_lat < 0.)
        oNMEA0183.GPwpl.Position.Latitude.Set(-prp->m_lat, "S");
      else
        oNMEA0183.GPwpl.Position.Latitude.Set(prp->m_lat, "N");

      if (prp->m_lon < 0.)
        oNMEA0183.GPwpl.Position.Longitude.Set(-prp->m_lon, "W");
      else
        oNMEA0183.GPwpl.Position.Longitude.Set(prp->m_lon, "E");

      wxString name = qs2ws(prp->GetName());
      name += "000000";
      name.Truncate(g_maxWPNameLength);
      oNMEA0183.GPwpl.To = name;

      oNMEA0183.GPwpl.Write(snt);
    }

    auto msg_out = std::make_shared<Nmea0183Msg>(
        "ECWPL", snt.Sentence.ToStdString(), address);
    drv_n0183->SendMessage(msg_out, address);

    NavmsgStatus ns;
    ns.direction = NavmsgStatus::Direction::kOutput;
    multiplexer.LogOutputMessage(msg_out, ns);
    auto msg = wxString("-->GPS Port:") + com_name + " Sentence: ";
    msg.Trim();
    wxLogMessage(msg);

    if (g_GPS_Ident == "FurunoGP3X") {
      wxString term;
      term.Printf("$PFEC,GPxfr,CTL,E%c%c", 0x0d, 0x0a);

      // driver->SendSentence(term);
      // LogOutputMessage(term, dstr->GetPort(), false);

      auto logmsg = wxString("-->GPS Port:") + com_name + " Sentence: " + term;
      logmsg.Trim();
      wxLogMessage(logmsg);
    }
    dlg_ctx.set_value(100);

    wxMilliSleep(500);

    ret_val = 0;
  }

ret_point:
  //  All finished with the temp port
  if (btempStream) registry.Deactivate(target_driver);

  if (b_restoreStream) {
    MakeCommDriver(&params_save);
  }
  return ret_val;
}
