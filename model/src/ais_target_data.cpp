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
 * Implement ais_target_data.h -- AIS target definitions
 */

#include <unordered_map>

#include <QString>

#include <QDateTime>
#include <wx/intl.h>
#include <wx/string.h>

#include "model/ais_state_vars.h"
#include "model/ais_target_data.h"
#include "model/config_vars.h"
#include "model/navutil_base.h"
#include "model/own_ship.h"

// Helper: convert a wxString (typically the result of the _() translation
// macro, still in use pending P3.10) into a QString.
static inline QString FromWx(const wxString &s) {
  return QString::fromStdString(s.utf8_string());
}

static std::unordered_map<int, QString> s_ERI_hash;

void make_hash_ERI(int key, const QString &description) {
  s_ERI_hash[key] = description;
}

void clear_hash_ERI() { s_ERI_hash.clear(); }

static QString FormatTimeAdaptive(int seconds) {
  int m = seconds / 60;
  if (seconds < 100)
    return QString::asprintf("%3ds", seconds);
  else if (seconds < 3600) {
    int m = seconds / 60;
    int s = seconds % 60;
    return QString::asprintf("%2dmin %02ds", m, s);
  }
  int h = seconds / 3600;
  m -= h * 60;
  return QString::asprintf("%2dh %02dmin", h, m);
}

static QString html_escape(const QString &src) {
  // Escape &, <, > as well as single and double quotes for HTML.
  QString ret = src;

  ret.replace("<", "&lt;");
  ret.replace(">", "&gt;");

  // only < and > in 6 bits AIS ascii
  // ret.replace("\"", "&quot;");
  // ret.replace("&", "&amp;");
  // ret.replace("'", "&#39;");

  // Do we care about multiple spaces?
  //   ret.replace(" ", "&nbsp;");
  return ret;
}

QString trimAISField(char *data) {
  //  Clip any unused characters (@) from data

  QString field = QString::fromLatin1(data);
  while (field.endsWith('@') || field.endsWith(' ')) field.chop(1);

  //  And remove any leading spaces to properly sort and display
  while (field.startsWith(' ')) field.remove(0, 1);

  return field;
}

QString ais_get_status(int index) {
  static const wxString ais_status[] = {
      _("Underway using Engine"),
      _("At Anchor"),
      _("Not Under Command"),
      _("Restricted Manoeuvrability"),
      _("Constrained by draught"),
      _("Moored"),
      _("Aground"),
      _("Engaged in Fishing"),
      _("Underway Sailing"),
      _("High Speed Craft"),
      _("Wing In Ground Effect"),
      _("Power-driven vessel towing astern (regional use)"),
      _("Power-driven vessel pushing ahead or towing alongside (regional use)"),
      _("Reserved 13"),
      _("Reserved 14"),
      _("Undefined"),
      _("AtoN Virtual"),
      _("AtoN Virtual (On Position)"),
      _("AtoN Virtual (Off Position)"),
      _("AtoN Real"),
      _("AtoN Real (On Position)"),
      _("AtoN Real(Off Position)")};

  return FromWx(ais_status[index]);
}

static QString ais_meteo_get_trend(int tend) {
  QString trend = "";
  if (tend < 3) {
    if (tend == 0)
      trend = FromWx(_("steady"));
    else if (tend == 1)
      trend = FromWx(_("decreasing"));
    else if (tend == 2)
      trend = FromWx(_("increasing"));
  }
  return trend;
}

static QString aisMeteoPrecipType(int precip) {
  QString prec = "";
  switch (precip) {
    case 0:
      prec = "Reserved";
      break;
    case 1:
      prec = "Rain";
      break;
    case 2:
      prec = "Thunderstorm";
      break;
    case 3:
      prec = "Freezing rain";
      break;
    case 4:
      prec = "Mixed / ice";
      break;
    case 5:
      prec = "Snow";
      break;
    default:
      prec = "not available";
      // 6 = reserved 7 = not available = default
  }
  return prec;
}

static QString aisMeteoWaterLevelRef(int refID) {
  QString ref = "";
  switch (refID) {
    case 0:
      ref = "MLLW";
      break;
    case 1:
      ref = "IGLD-85";
      break;
    case 2:
      ref = "Local river";
      break;
    case 3:
      ref = "STND";
      break;
    case 4:
      ref = "MHHW";
      break;
    case 5:
      ref = "MHW";
      break;
    case 6:
      ref = "MSL";
      break;
    case 7:
      ref = "MLW";
      break;
    case 8:
      ref = "NGVD-29";
      break;
    case 9:
      ref = "NAVD-88";
      break;
    case 10:
      ref = "WGS-84";
      break;
    case 11:
      ref = "LAT";
      break;
    case 12:
      ref = "Pool";
      break;
    case 13:
      ref = "Gauge";
      break;
  }
  return ref;
}

AisTargetData::AisTargetData(AisTargetCallbacks cb) : m_callbacks(cb) {
  strncpy(ShipName, "Unknown             ", SHIP_NAME_LEN);
  strncpy(CallSign, "       ", 8);
  strncpy(Destination, "                    ", DESTINATION_LEN);
  ShipNameExtension[0] = 0;
  b_show_AIS_CPA = false;

  SOG = 555.;
  COG = 666.;
  HDG = 511.;
  ROTAIS = -128;
  Lat = 0.;
  Lon = 0.;

  QDateTime now = QDateTime::currentDateTimeUtc();
  PositionReportTicks = now.toSecsSinceEpoch();  // Default is my idea of NOW
  StaticReportTicks = now.toSecsSinceEpoch();
  b_lost = false;
  b_removed = false;

  IMO = 0;
  MID = 555;
  MMSI = 666;
  NavStatus = UNDEFINED;
  SyncState = 888;
  SlotTO = 999;
  ShipType = 19;  // "Unknown"
  b_isDSCtarget = false;
  m_dscNature = 99;
  m_dscTXmmsi = 666;

  CPA = 100;  // Large values avoid false alarms
  TCPA = 100;

  Range_NM = -1.;
  Brg = -1.;

  DimA = DimB = DimC = DimD = 0;
  ;

  ETA_Mo = 0;
  ETA_Day = 0;
  ETA_Hr = 24;
  ETA_Min = 60;

  Draft = 0.;

  RecentPeriod = 0;

  m_utc_hour = 0;
  m_utc_min = 0;
  m_utc_sec = 0;

  Class = AIS_CLASS_A;  // default
  n_alert_state = AIS_NO_ALERT;
  b_suppress_audio = false;
  b_positionDoubtful = false;
  b_positionOnceValid = false;
  b_nameValid = false;

  Euro_Length = 0;  // Extensions for European Inland AIS
  Euro_Beam = 0;
  Euro_Draft = 0;
  strncpy(Euro_VIN, "       ", 8);
  UN_shiptype = 0;

  b_isEuroInland = false;
  b_blue_paddle = false;

  b_NoTrack = false;
  b_OwnShip = false;
  b_PersistTrack = false;
  b_mPropPersistTrack = false;
  b_in_ack_timeout = false;

  b_active = false;
  blue_paddle = 0;
  bCPA_Valid = false;
  b_isFollower = false;
  ROTIND = 0;
  b_show_track = g_bAISShowTracks;
  b_SarAircraftPosnReport = false;
  altitude = 0;
  b_nameFromCache = false;
  importance = 0.0;
  for (unsigned int i = 0; i < AIS_TARGETDATA_MAX_CANVAS; i++)
    last_scale[i] = 50;
  met_data.original_mmsi = 0;
  met_data.stationID = 0;
  met_data.month = 0;
  met_data.day = 0;
  met_data.hour = 24;
  met_data.minute = 60;
  met_data.pos_acc = 1;
  met_data.wind_kn = 122;
  met_data.wind_gust_kn = 127;
  met_data.wind_dir = 360;
  met_data.wind_gust_dir = 360;
  met_data.air_temp = -102.4;
  met_data.rel_humid = 101;
  met_data.dew_point = 50.1;
  met_data.airpress = 1310;
  met_data.airpress_tend = 3;
  met_data.hor_vis = 12.7;
  met_data.hor_vis_GT = false;
  met_data.water_lev_dev = 4001 / 100 - 10;
  met_data.water_level = -32;
  met_data.water_lev_trend = 3;
  met_data.current = 25.5;
  met_data.curr_dir = 360;
  met_data.wave_height = 25.5;
  met_data.wave_period = 63;
  met_data.wave_dir = 360;
  met_data.swell_height = 25.5;
  met_data.swell_per = 63;
  met_data.swell_dir = 360;
  met_data.seastate = 13;
  met_data.water_temp = 501;
  met_data.precipitation = 7;
  met_data.salinity = 51.;
  met_data.ice = 3;
  met_data.vertical_ref = 14;
}

void AisTargetData::CloneFrom(AisTargetData *q) {
  strncpy(ShipName, q->ShipName, SHIP_NAME_LEN);
  strncpy(CallSign, q->CallSign, 8);
  strncpy(Destination, q->Destination, DESTINATION_LEN);
  ShipNameExtension[0] = 0;
  b_show_AIS_CPA = q->b_show_AIS_CPA;
  ;

  SOG = q->SOG;
  COG = q->COG;
  HDG = q->HDG;
  ROTAIS = q->ROTAIS;
  Lat = q->Lat;
  Lon = q->Lon;

  PositionReportTicks = q->PositionReportTicks;
  StaticReportTicks = q->StaticReportTicks;
  b_lost = q->b_lost;
  b_removed = q->b_removed;

  IMO = q->IMO;
  MID = q->MID;
  MMSI = q->MMSI;
  NavStatus = q->NavStatus;
  SyncState = q->SyncState;
  SlotTO = q->SlotTO;
  ShipType = q->ShipType;
  b_isDSCtarget = q->b_isDSCtarget;
  m_dscNature = q->m_dscNature;
  m_dscTXmmsi = q->m_dscTXmmsi;

  CPA = q->CPA;
  TCPA = q->TCPA;

  Range_NM = q->Range_NM;
  Brg = q->Brg;

  DimA = q->DimA;
  DimB = q->DimB;
  DimC = q->DimC;
  DimD = q->DimD;

  ETA_Mo = q->ETA_Mo;
  ETA_Day = q->ETA_Day;
  ETA_Hr = q->ETA_Hr;
  ETA_Min = q->ETA_Min;

  Draft = q->Draft;

  RecentPeriod = q->RecentPeriod;

  m_utc_hour = q->m_utc_hour;
  m_utc_min = q->m_utc_min;
  m_utc_sec = q->m_utc_sec;

  Class = q->Class;
  n_alert_state = q->n_alert_state;
  b_suppress_audio = q->b_suppress_audio;
  b_positionDoubtful = q->b_positionDoubtful;
  b_positionOnceValid = q->b_positionOnceValid;
  b_nameValid = q->b_nameValid;

  Euro_Length = q->Euro_Length;  // Extensions for European Inland AIS
  Euro_Beam = q->Euro_Beam;
  Euro_Draft = q->Euro_Draft;
  memcpy(Euro_VIN, q->Euro_VIN, EURO_VIN_LEN);
  UN_shiptype = q->UN_shiptype;

  b_isEuroInland = q->b_isEuroInland;
  b_blue_paddle = q->b_blue_paddle;

  b_OwnShip = q->b_OwnShip;
  b_in_ack_timeout = q->b_in_ack_timeout;

  m_ptrack = q->m_ptrack;

  b_active = q->b_active;
  blue_paddle = q->blue_paddle;
  bCPA_Valid = q->bCPA_Valid;
  ROTIND = q->ROTIND;
  b_show_track = q->b_show_track;
  b_SarAircraftPosnReport = q->b_SarAircraftPosnReport;
  altitude = q->altitude;
}

AisTargetData::~AisTargetData() { m_ptrack.clear(); }
// AisTargetData::~AisTargetData() { m_pMetPoint.clear(); }  //TODO Needed?

QString AisTargetData::GetFullName() {
  QString retName;
  if (b_nameValid) {
    QString shipName = trimAISField(ShipName);
    if (shipName == "Unknown")
      retName = FromWx(wxGetTranslation(wxString::FromUTF8(shipName.toStdString())));
    else
      retName = shipName;

    if (strlen(ShipNameExtension)) {
      QString shipNameExt = trimAISField(ShipNameExtension);
      retName += shipNameExt;
    }
  }

  return retName;
}

QString AisTargetData::BuildQueryResult() {
  QString html;
  QDateTime now = QDateTime::currentDateTime();

  QString tableStart =
      "\n<table width=100% border=0 cellpadding=1 cellspacing=0>\n";

  QString tableEnd = "</table>\n\n";
  QString rowStart = "<tr><td><font size=-2>";
  QString rowStartH = "<tr><td nowrap>";
  QString rowSeparator = "</font></td><td></td><td><b>";
  QString rowSeparatorH = "</td><td></td><td>";
  QString colSeparator = "<td></td>";
  QString rowEnd = "</b></td></tr>\n";
  QString vertSpacer = "<tr><td></td></tr>\n\n";

  QString IMOstr, MMSIstr, ClassStr;

  html += tableStart + "<tr><td nowrap colspan=2>";
  if (b_nameValid) {
    html += "<font size=+2><i><b>" + GetFullName();
    html += "</b></i></font>&nbsp;&nbsp;<b>";
  }

  if ((Class != AIS_ATON) && (Class != AIS_BASE) && (Class != AIS_GPSG_BUDDY) &&
      (Class != AIS_SART) && (Class != AIS_METEO)) {
    html += trimAISField(CallSign) + "</b>" + rowEnd;

    if (Class != AIS_CLASS_B) {
      if (IMO > 0) IMOstr = QString::asprintf("%08d", abs(IMO));
    }
  } else
    html += "</b>" + rowEnd;

  html += vertSpacer;

  if (Class != AIS_GPSG_BUDDY) {
    MMSIstr = QString::asprintf("%09d", abs(MMSI));
  }
  ClassStr =
      FromWx(wxGetTranslation(wxString::FromUTF8(Get_class_string(false).toStdString())));

  if (Class == AIS_ATON) {
    QString cls("AtoN: ");
    cls += Get_vessel_type_string(false);
    ClassStr = FromWx(wxGetTranslation(wxString::FromUTF8(cls.toStdString())));
  }

  if (b_SarAircraftPosnReport) {
    int airtype = (MMSI % 1000) / 100;
    ClassStr = FromWx(airtype == 5 ? _("SAR Helicopter") : _("SAR Aircraft"));
  }

  if (IMOstr.length())
    html += "<tr><td colspan=2><table width=100% border=0 cellpadding=0 "
            "cellspacing=0>" +
            rowStart + FromWx(_("MMSI")) +
            "</font></td><td>&nbsp;</td><td><font size=-2>" +
            FromWx(_("Class")) +
            "</font></td><td>&nbsp;</td><td align=right><font size=-2>" +
            FromWx(_("IMO")) + "</font></td></tr>" + rowStartH + "<b>" +
            MMSIstr + "</b></td><td>&nbsp;</td><td><b>" + ClassStr +
            "</b></td><td>&nbsp;</td><td align=right><b>" + IMOstr + rowEnd +
            "</table></td></tr>";

  else if (Class == AIS_METEO) {
    MMSIstr = QString::asprintf("%09d", abs(met_data.original_mmsi));
    html += "<tr><td colspan=2><table width=100% border=0 cellpadding=0 "
            "cellspacing=0>" +
            rowStart + FromWx(_("MMSI")) +
            "</font></td><td>&nbsp;</td><td align=right><font size=-2>" +
            FromWx(_("Class")) + "</font></td></tr>" + rowStartH + "<b>" +
            MMSIstr + "</b></td><td>&nbsp;</td><td align=right><b>" +
            "<font size=-1>" + ClassStr + rowEnd + rowStart +
            "<b>ID: " + QString::number(MMSI);
    if (met_data.stationID) {  // Facilitate to find a Meteo target on SignalK
      QString SK_ID = QString::asprintf("%06d", (met_data.stationID - 1000000));
      html += QString("<td>&nbsp;</td><td align=right>") + "SK-ID: " + SK_ID;
    }
    html += rowEnd + "</b></table></td></tr>";
  } else
    html += "<tr><td colspan=2><table width=100% border=0 cellpadding=0 "
            "cellspacing=0>" +
            rowStart + FromWx(_("MMSI")) +
            "</font></td><td>&nbsp;</td><td align=right><font size=-2>" +
            FromWx(_("Class")) + "</font></td></tr>" + rowStartH + "<b>" +
            MMSIstr + "</b></td><td>&nbsp;</td><td align=right><b>" + ClassStr +
            rowEnd + "</table></td></tr>";
  html += "<tr><td colspan=2><table width=100% border=0 cellpadding=0 "
          "cellspacing=0>" +
          rowStart;
  if ((Class != AIS_SART) && (Class != AIS_ARPA)) {
    html += FromWx((Class == AIS_BASE || Class == AIS_ATON ||
                    Class == AIS_METEO)
                       ? _("Nation")
                       : _("Flag")) +
            rowEnd + "</font></td></tr>" + rowStartH + "<font size=-1><b>" +
            GetCountryCode(true);
  }
  if (Class == AIS_CLASS_B && MMSIstr.startsWith("8")) {
    html += QString("<td align=right>") + FromWx(_("Handheld"));
  }
  html += rowEnd + "</font></table></td></tr>";

  QString navStatStr;
  if ((Class != AIS_BASE) && (Class != AIS_CLASS_B) && (Class != AIS_SART) &&
      (Class != AIS_METEO)) {
    html += vertSpacer;
    if ((NavStatus <= 21) && (NavStatus >= 0))
      navStatStr = FromWx(
          wxGetTranslation(wxString::FromUTF8(ais_get_status(NavStatus).toStdString())));
  } else if (Class == AIS_SART) {
    if (NavStatus == RESERVED_14)
      navStatStr = FromWx(_("Active"));
    else if (NavStatus == UNDEFINED)
      navStatStr = FromWx(_("Testing"));
  }

  QString sart_sub_type;
  if (Class == AIS_SART) {
    int mmsi_start = MMSI / 1000000;
    switch (mmsi_start) {
      case 970:
        //                sart_sub_type = "SART";
        break;
      case 972:
        sart_sub_type = "MOB";
        break;
      case 974:
        sart_sub_type = "EPIRB";
        break;
      default:
        sart_sub_type = FromWx(_("Unknown"));
        break;
    }
  }

  QString AISTypeStr, UNTypeStr, sizeString;
  if ((Class != AIS_BASE) && (Class != AIS_SART) && (Class != AIS_DSC) &&
      (Class != AIS_METEO)) {
    //      Ship type
    AISTypeStr = FromWx(
        wxGetTranslation(wxString::FromUTF8(Get_vessel_type_string().toStdString())));

    if (b_isEuroInland && UN_shiptype) {
      auto it = s_ERI_hash.find(UN_shiptype);
      QString type;
      if (it == s_ERI_hash.end())
        type = FromWx(_("Undefined"));
      else
        type = it->second;

      UNTypeStr = FromWx(wxGetTranslation(wxString::FromUTF8(type.toStdString())));
    }

    if (b_SarAircraftPosnReport) {
      AISTypeStr.clear();
      UNTypeStr.clear();
      navStatStr.clear();
    }

    //  Dimensions

    if (NavStatus != ATON_VIRTUAL && Class != AIS_ARPA && Class != AIS_APRS &&
        Class != AIS_BUOY) {
      if ((Class == AIS_CLASS_B) || (Class == AIS_ATON)) {
        sizeString =
            QString::asprintf("%dm x %dm", (DimA + DimB), (DimC + DimD));
      } else if (!b_SarAircraftPosnReport) {
        if ((DimA + DimB + DimC + DimD) == 0) {
          if (b_isEuroInland) {
            if (Euro_Length == 0.0) {
              if (Euro_Draft > 0.01) {
                sizeString += QString::asprintf("---m x ---m x %4.1fm",
                                                Euro_Draft);
              } else {
                sizeString += "---m x ---m x ---m";
              }
            } else {
              if (Euro_Draft > 0.01) {
                sizeString += QString::asprintf("%5.1fm x %4.1fm x %4.1fm",
                                                Euro_Length, Euro_Beam,
                                                Euro_Draft);
              } else {
                sizeString += QString::asprintf("%5.1fm x %4.1fm x ---m\n\n",
                                                Euro_Length, Euro_Beam);
              }
            }
          } else {
            if (Draft > 0.01) {
              sizeString += QString::asprintf("---m x ---m x %4.1fm", Draft);
            } else {
              sizeString += "---m x ---m x ---m";
            }
          }
        } else if (Draft < 0.01) {
          sizeString += QString::asprintf("%dm x %dm x ---m", (DimA + DimB),
                                          (DimC + DimD));
        } else {
          sizeString += QString::asprintf("%dm x %dm x %4.1fm", (DimA + DimB),
                                          (DimC + DimD), Draft);
        }
      }
    }
  }

  if (Class == AIS_SART) {
    html += QString("<tr><td colspan=2>") + "<b>" + AISTypeStr;
    if (sart_sub_type.length())
      html += QString(" (") + sart_sub_type + "), ";
    html += navStatStr;
    html += rowEnd + "<tr><td colspan=2>" + "<b>" + sizeString + rowEnd;
  }

  else if (Class == AIS_ATON) {
    html += QString("<tr><td colspan=2>") + "<b>" + navStatStr;
    html += rowEnd + "<tr><td colspan=2>" + "<b>" + sizeString + rowEnd;
  } else if (Class == AIS_DSC && (ShipType == 12 || ShipType == 16)) {
    if (ShipType == 16) {  // Distress relay
      html += QString("<tr><td colspan=2>") + "<b>" + FromWx(_("Distress relay"));
      if (m_dscTXmmsi > 2000000) {
        QString mmsirelay = QString::asprintf(" %09d", abs(m_dscTXmmsi));
        html += QString(" ") + FromWx(_("by:")) + mmsirelay;
      }
      html += QString("<b>") + sizeString + rowEnd;
    }
    html += QString("<tr><td colspan=2>") + FromWx(_("Nature of distress: ")) +
            rowEnd + "<tr><td colspan=2>";
    if (m_dscNature < 13) {
      html += QString("<tr><td colspan=2>") + "<b>" +
              GetNatureofDistress(m_dscNature) + "<b>" + sizeString + rowEnd +
              "<tr><td colspan=2>";
    }
  } else if ((Class != AIS_BASE) && (Class != AIS_DSC)) {
    html += QString("<tr><td colspan=2>") + "<b>" + AISTypeStr;
    if (navStatStr.length()) html += QString(", ") + navStatStr;
    if (UNTypeStr.length()) html += QString(" (UN Type ") + UNTypeStr + ")";
    html += rowEnd + "<tr><td colspan=2>" + "<b>" + sizeString + rowEnd;
  }

  if (MSG_14_text.length()) {
    // The safety message is displayed for all target's AIS Query
    // For an Active SART a Target Alert is also created.
    html += rowStart + "<tr><td colspan=1>" +
            FromWx(_("Safety Broadcast Message")) + rowEnd + rowStartH + "<b>" +
            MSG_14_text + "</b>" + rowEnd;
  }

  if (b_positionOnceValid) {
    QString posTypeStr;
    if (b_positionDoubtful) posTypeStr += FromWx(_(" (Last Known)"));

    now = now.toUTC();
    int target_age = now.toSecsSinceEpoch() - PositionReportTicks;

    html += vertSpacer + rowStart + FromWx(_("Position")) + posTypeStr +
            "</font></td><td align=right><font size=-2>" +
            FromWx(_("Report Age")) + "</font></td></tr>"

            + rowStartH + "<b>" + FromWx(toSDMM(1, Lat)) +
            "</b></td><td align=right><b>" + FormatTimeAdaptive(target_age) +
            rowEnd + rowStartH + "<b>" + FromWx(toSDMM(2, Lon));
    if (Class != AIS_METEO)
      html += rowEnd;
    else {
      QString meteoTime =
          QString::asprintf(" %02d:%02d", met_data.hour, met_data.minute);
      html += QString(" </td><td align=right></b></font><font size=-3>") +
              FromWx(_("Issued (UTC)")) + "</font><font size=-1><b>" +
              meteoTime + "</font>" + rowEnd;
    }
  }

  QString courseStr, sogStr, hdgStr, rotStr, rngStr, brgStr, destStr, etaStr;

  if (Class == AIS_GPSG_BUDDY) {
    long month, year, day;
    day = m_date_string.mid(0, 2).toLong();
    month = m_date_string.mid(2, 2).toLong();
    year = m_date_string.mid(4, 2).toLong();
    QDateTime date(QDate(year + 2000, month, day), QTime(0, 0));

    QString f_date = date.date().toString(Qt::ISODate);

    html += vertSpacer + rowStart + FromWx(_("Report as of")) + rowEnd +
            rowStartH + "<b>" + f_date + "</b> at <b>" +
            QString::asprintf("%d:%d UTC ", m_utc_hour, m_utc_min) + rowEnd;
  } else {
    if (Class == AIS_CLASS_A && !b_SarAircraftPosnReport) {
      html += vertSpacer + rowStart + FromWx(_("Destination")) +
              "</font></td><td align=right><font size=-2>" +
              FromWx(_("ETA (UTC)")) + "</font></td></tr>\n" + rowStartH +
              "<b>";
      QString dest = trimAISField(Destination);
      if (dest.length())
        html += html_escape(dest);
      else
        html += "---";
      html += "</b></td><td nowrap align=right><b>";

      if ((ETA_Mo) && (ETA_Hr < 24)) {
        int yearOffset = 0;
        if (now.date().month() > ETA_Mo) yearOffset = 1;
        QDateTime eta(QDate(now.date().year() + yearOffset, ETA_Mo, ETA_Day),
                      QTime(ETA_Hr, ETA_Min));
        html += eta.toString("MMM dd HH:mm");
      } else
        html += "---";
      html += rowEnd;
    }

    if (Class == AIS_CLASS_A || Class == AIS_CLASS_B || Class == AIS_ARPA ||
        Class == AIS_APRS || Class == AIS_SART || Class == AIS_BUOY) {
      int crs = qRound(COG);
      if (crs < 360) {
        QString magString, trueString;
        if (g_bShowMag)
          magString += QString::asprintf(
              "%03d%c(M)", static_cast<int>(m_callbacks.get_mag(COG)),
              0x00B0);
        if (g_bShowTrue)
          trueString += QString::asprintf("%03d%c ", (int)crs, 0x00B0);

        courseStr += trueString + magString;
      } else if (COG == 360.0)
        courseStr = "---";
      else if (crs == 360)
        courseStr = "0&deg;";

      double speed_show = toUsrSpeed(SOG);

      if ((SOG <= 102.2) || b_SarAircraftPosnReport) {
        if (speed_show < 10.0)
          sogStr =
              QString::asprintf("%.2f ", speed_show) + FromWx(getUsrSpeedUnit());
        else if (speed_show < 100.0)
          sogStr =
              QString::asprintf("%.1f ", speed_show) + FromWx(getUsrSpeedUnit());
        else
          sogStr =
              QString::asprintf("%.0f ", speed_show) + FromWx(getUsrSpeedUnit());
      } else
        sogStr = "---";

      if ((int)HDG != 511)
        hdgStr = QString::asprintf("%03d&deg;", (int)HDG);
      else
        hdgStr = "---";

      if (ROTAIS != -128) {
        if (ROTAIS == 127)
          rotStr += QString("> 5&deg;/30s ") + FromWx(_("Right"));
        else if (ROTAIS == -127)
          rotStr += QString("> 5&deg;/30s ") + FromWx(_("Left"));
        else {
          if (ROTIND > 0)
            rotStr += QString::asprintf("%3d&deg;/Min ", ROTIND) +
                      FromWx(_("Right"));
          else if (ROTIND < 0)
            rotStr += QString::asprintf("%3d&deg;/Min ", -ROTIND) +
                      FromWx(_("Left"));
          else
            rotStr = "0";
        }
      } else if (!b_SarAircraftPosnReport)
        rotStr = "---";
    }
  }

  if (b_positionOnceValid && bGPSValid && (Range_NM >= 0.))
    rngStr = FromWx(FormatDistanceAdaptive(Range_NM));
  else
    rngStr = "---";

  int brg = (int)qRound(Brg);
  if (Brg > 359.5) brg = 0;
  if (b_positionOnceValid && bGPSValid && (Brg >= 0.) && (Range_NM > 0.) &&
      (fabs(Lat) < 85.)) {
    QString magString, trueString;
    if (g_bShowMag)
      magString += QString::asprintf(
          "%03d%c(M)", static_cast<int>(m_callbacks.get_mag(Brg)), 0x00B0);
    if (g_bShowTrue)
      trueString += QString::asprintf("%03d%c ", (int)Brg, 0x00B0);

    brgStr += trueString + magString;
  } else
    brgStr = "---";

  QString turnRateHdr;  // Blank if ATON or BASE or Special Position Report (9)
  if ((Class != AIS_ATON) && (Class != AIS_BASE) && (Class != AIS_DSC) &&
      (Class != AIS_METEO)) {
    html += vertSpacer +
            "<tr><td colspan=2><table width=100% border=0 cellpadding=0 "
            "cellspacing=0>" +
            rowStart + FromWx(_("Speed")) +
            "</font></td><td>&nbsp;</td><td><font size=-2>" +
            FromWx(_("Course")) +
            "</font></td><td>&nbsp;</td><td align=right><font size=-2>";
    if (!b_SarAircraftPosnReport) html += FromWx(_("Heading"));

    html += QString("</font></td></tr>") + rowStartH + "<b>" + sogStr +
            "</b></td><td>&nbsp;</td><td><b>" + courseStr +
            "</b></td><td>&nbsp;</td><td align=right><b>";
    if (!b_SarAircraftPosnReport) html += hdgStr;
    html += rowEnd + "</table></td></tr>" + vertSpacer;

    if (!b_SarAircraftPosnReport) turnRateHdr = FromWx(_("Turn Rate"));
  }
  if (Class != AIS_METEO) {
    html += QString("<tr><td colspan=2><table width=100% border=0 "
                    "cellpadding=0 cellspacing=0>") +
            rowStart + FromWx(_("Range")) +
            "</font></td><td>&nbsp;</td><td><font size=-2>" +
            FromWx(_("Bearing")) +
            "</font></td><td>&nbsp;</td><td align=right><font size=-2>" +
            turnRateHdr + "</font></td></tr>" + rowStartH + "<b>" + rngStr +
            "</b></td><td>&nbsp;</td><td><b>" + brgStr +
            "</b></td><td>&nbsp;</td><td align=right><b>";
    if (!b_SarAircraftPosnReport) html += rotStr;
    html += rowEnd + "</table></td></tr>" + vertSpacer;
  }

  if (bCPA_Valid && Class != AIS_METEO) {
    QString tcpaStr;
    tcpaStr += QString("</b> ") + FromWx(_("in ")) +
               "</td><td align=right><b>" +
               FormatTimeAdaptive((int)(TCPA * 60.));

    html += /*vertSpacer + */ rowStart + "<font size=-2>" + FromWx(_("CPA")) +
            "</font>" + rowEnd + rowStartH + "<b>" +
            FromWx(FormatDistanceAdaptive(CPA)) + tcpaStr + rowEnd;
  }

  if (Class != AIS_BASE && Class != AIS_METEO) {
    if (blue_paddle == 1) {
      html += rowStart + FromWx(_("Inland Blue Flag")) + rowEnd + rowStartH +
              "<b>" + FromWx(_("Clear")) + rowEnd;
    } else if (blue_paddle == 2) {
      html += rowStart + FromWx(_("Inland Blue Flag")) + rowEnd + rowStartH +
              "<b>" + FromWx(_("Set")) + rowEnd;
    }
  }

  if (b_SarAircraftPosnReport) {
    QString altStr;
    if (altitude != 4095)
      altStr = QString::asprintf("%4d m", altitude);
    else
      altStr = FromWx(_("Unknown"));

    html += rowStart + FromWx(_("Altitude")) +
            "</font></td><td>&nbsp;</td><td><font size=-0>" + rowStartH +
            "<b>" + altStr + "</b></td><td>&nbsp;</td><td><b>" + rowEnd +
            "</table></td></tr>" + vertSpacer;
  }

  if (Class == AIS_METEO) {
    if (met_data.wind_kn < 122) {
      double userwindspeed = toUsrWindSpeed(met_data.wind_kn);
      QString wspeed = FromWx(wxString::Format(
          "%0.1f %s %d%c", userwindspeed, getUsrWindSpeedUnit(),
          met_data.wind_dir, 0x00B0));

      double userwindgustspeed = toUsrWindSpeed(met_data.wind_gust_kn);
      QString wspeedGust = FromWx(wxString::Format(
          "%.0f %s %d%c", userwindgustspeed, getUsrWindSpeedUnit(),
          met_data.wind_gust_dir, 0x00B0));
      if (met_data.wind_gust_kn >= 126) wspeedGust = "";

      html += vertSpacer + rowStart + FromWx(_("Wind speed")) +
              "</font></td><td align=right><font size=-2>" +
              FromWx(_("Wind gust")) + "</font></td></tr>" + rowStartH + "<b>" +
              wspeed + "</b></td><td align=right><b>" + wspeedGust + rowEnd;
    }

    if (met_data.water_lev_dev < 30. || met_data.water_level > -32. ||
        met_data.current < 25.5) {
      QString wlevel_txt = FromWx(_("Water level deviation"));
      QString wlevel;
      if (met_data.water_lev_dev < 30.) {
        double userlevel = toUsrDepth(met_data.water_lev_dev);
        wlevel = FromWx(wxString::Format(
            "%.1f %s %s", userlevel, getUsrDepthUnit(),
            wxString::FromUTF8(ais_meteo_get_trend(met_data.water_lev_trend)
                         .toStdString())));
        if (met_data.vertical_ref < 14) {
          wlevel_txt = FromWx(_("Water level dev. Ref: "));
          wlevel_txt += aisMeteoWaterLevelRef(met_data.vertical_ref);
        }

        if (met_data.water_lev_dev >= 30.) wlevel = "";

      } else if (met_data.water_level > -32.) {
        double userlevel = toUsrDepth(met_data.water_level);
        wlevel = FromWx(wxString::Format(
            "%.1f %s %s", userlevel, getUsrDepthUnit(),
            wxString::FromUTF8(ais_meteo_get_trend(met_data.water_lev_trend)
                         .toStdString())));
        wlevel_txt = FromWx(_("Water level"));
        if (met_data.water_level <= -32.) wlevel = "";
      }

      QString current = QString::asprintf(
          "%.1f kts %d%c", met_data.current, met_data.curr_dir, 0x00B0);
      if (met_data.current >= 25.5) current = "";

      html += vertSpacer + rowStart + wlevel_txt +
              "</font></td><td align=right><font size=-2>" +
              FromWx(_("Surface current ")) + "</font></td></tr>" + rowStartH +
              "<b>" + wlevel + "</b></td><td align=right><b>" + current +
              rowEnd;
    }

    if (met_data.wave_height < 24.6 || met_data.swell_height < 24.6) {
      double userwave = toUsrDepth(met_data.wave_height);
      QString wave = FromWx(wxString::Format(
          "%.1f %s %d%c %d %s ", userwave, getUsrDepthUnit(), met_data.wave_dir,
          0x00B0, met_data.wave_period, _("s")));
      if (met_data.wave_height >= 24.6) wave = "";

      double userswell = toUsrDepth(met_data.swell_height);
      QString swell = FromWx(wxString::Format(
          "%.1f %s %d%c %d %s", userswell, getUsrDepthUnit(),
          met_data.swell_dir, 0x00B0, met_data.swell_per, _("s")));
      if (met_data.swell_height >= 25.) swell = "";

      html += vertSpacer + rowStart + FromWx(_("Waves height & period")) +
              "</font></td><td align=right><font size=-2>" +
              FromWx(_("Swell height & period ")) + "</font></td></tr>" +
              rowStartH + "<b>" + wave + "</b></td><td align=right><b>" +
              swell + rowEnd;
    }

    if (met_data.air_temp != -102.4 || met_data.airpress < 1310) {
      double usertemp = toUsrTemp(met_data.air_temp);
      QString airtemp = FromWx(
          wxString::Format("%.1f%c%s", usertemp, 0x00B0, getUsrTempUnit()));
      if (met_data.air_temp == -102.4) airtemp = "";

      QString airpress = FromWx(wxString::Format(
          "%d hPa %s", met_data.airpress,
          wxString::FromUTF8(ais_meteo_get_trend(met_data.airpress_tend).toStdString())));
      const int ap = met_data.airpress;
      if (ap < 800 || ap >= 1310) airpress = "";

      html += vertSpacer + rowStart + FromWx(_("Air Temperatur")) +
              "</font></td><td align=right><font size=-2>" +
              FromWx(_("Air pressure")) + "</font></td></tr>" + rowStartH +
              "<b>" + airtemp + "</b></td><td align=right><b>" + airpress +
              rowEnd;
    }

    if (met_data.rel_humid < 101 || met_data.dew_point < 50.) {
      QString humid = QString::asprintf("%d%c", met_data.rel_humid, '%');
      if (met_data.rel_humid >= 101) humid = "";

      double usertempDew = toUsrTemp(met_data.dew_point);
      QString dewpoint = FromWx(
          wxString::Format("%.1f%c%s", usertempDew, 0x00B0, getUsrTempUnit()));
      if (met_data.dew_point >= 50.) dewpoint = "";

      html += vertSpacer + rowStart + FromWx(_("Relative Humidity")) +
              "</font></td><td align=right><font size=-2>" +
              FromWx(_("Dew Point ")) + "</font></td></tr>" + rowStartH +
              "<b>" + humid + "</b></td><td align=right><b>" + dewpoint +
              rowEnd;
    }

    if (met_data.water_temp < 50.1 || met_data.seastate < 13) {
      double usertemp = toUsrTemp(met_data.water_temp);
      QString watertemp = FromWx(
          wxString::Format("%.1f%c%s", usertemp, 0x00B0, getUsrTempUnit()));
      if (met_data.water_temp >= 50.1) watertemp = "";

      QString seastate = QString::asprintf("%d Bf ", met_data.seastate);
      if (met_data.seastate == 13) seastate = "";

      html += vertSpacer + rowStart + FromWx(_("Water Temperatur")) +
              "</font></td><td align=right><font size=-2>" +
              FromWx(_("Sea state")) + "</font></td></tr>" + rowStartH + "<b>" +
              watertemp + "</b></td><td align=right><b>" + seastate + rowEnd;
    }

    if (met_data.precipitation < 7 || met_data.hor_vis < 12.7) {
      QString precip = aisMeteoPrecipType(met_data.precipitation);
      if (met_data.precipitation >= 6) precip = "";

      double userVisDist = toUsrDistance(met_data.hor_vis);
      QString horVis = FromWx(wxString::Format(
          "%s%.1f %s", (met_data.hor_vis_GT ? ">" : ""), userVisDist,
          getUsrDistanceUnit()));
      if (met_data.hor_vis >= 12.7) horVis = "";
      html += vertSpacer + rowStart + FromWx(_("Precipitation")) +
              "</font></td><td align=right><font size=-2>" +
              FromWx(_("Horizontal Visibility")) + "</font></td></tr>" +
              rowStartH + "<b>" + precip + "</b></td><td align=right><b>" +
              horVis + rowEnd;
    }

    if (met_data.salinity < 50. || met_data.ice < 2) {
      QString sal = QString::asprintf("%.1f%c", met_data.salinity, 0x2030);
      if (met_data.salinity >= 50.) sal = "";

      QString icestatus = FromWx(_("No"));
      if (met_data.ice == 1) icestatus = FromWx(_("Yes"));
      if (met_data.ice >= 2) icestatus = "";

      html += vertSpacer + rowStart + FromWx(_("Sea salinity")) +
              "</font></td><td align=right><font size=-2>" +
              FromWx(_("Ice status")) + "</font></td></tr>" + rowStartH +
              "<b>" + sal + "</b></td><td align=right><b>" + icestatus +
              rowEnd;
    }
  }
  html += "</table>";
  return html;
}

QString AisTargetData::GetRolloverString() {
  QString result;
  QString t;
  if (b_nameValid) {
    result.append("\"");
    result.append(GetFullName());
    result.append("\" ");
  }
  if (Class != AIS_GPSG_BUDDY) {
    t = QString::asprintf("%09d", abs(MMSI));
    result.append(t);
    result.append(" ");
    result.append(GetCountryCode(false));
  }
  t = trimAISField(CallSign);
  if (t.length()) {
    result.append(" (");
    result.append(t);
    result.append(")");
  }
  if (g_bAISRolloverShowClass || (Class == AIS_SART)) {
    if (result.length()) result.append("\n");
    result.append("[");
    if (Class == AIS_ATON) {
      result.append(FromWx(
          wxGetTranslation(wxString::FromUTF8(Get_class_string(true).toStdString()))));
      result.append(": ");
      result.append(FromWx(wxGetTranslation(
          wxString::FromUTF8(Get_vessel_type_string(false).toStdString()))));
    } else if (b_SarAircraftPosnReport) {
      int airtype = (MMSI % 1000) / 100;
      result.append(
          FromWx(airtype == 5 ? _("SAR Helicopter") : _("SAR Aircraft")));
    } else
      result.append(FromWx(
          wxGetTranslation(wxString::FromUTF8(Get_class_string(false).toStdString()))));

    result.append("] ");
    if ((Class != AIS_ATON) && (Class != AIS_BASE)) {
      if (Class == AIS_SART) {
        int mmsi_start = MMSI / 1000000;
        switch (mmsi_start) {
          case 970:
            break;
          case 972:
            result += "MOB";
            break;
          case 974:
            result += "EPIRB";
            break;
          default:
            result += FromWx(_("Unknown"));
            break;
        }
      }

      if (Class != AIS_SART && Class != AIS_METEO) {
        if (!b_SarAircraftPosnReport)
          result.append(FromWx(wxGetTranslation(
              wxString::FromUTF8(Get_vessel_type_string(false).toStdString()))));
      }

      if ((Class != AIS_CLASS_B) && (Class != AIS_SART) && Class != AIS_DSC &&
          Class != AIS_METEO && !b_SarAircraftPosnReport) {
        if ((NavStatus <= 15) && (NavStatus >= 0)) {
          result.append(" (");
          result.append(FromWx(wxGetTranslation(
              wxString::FromUTF8(ais_get_status(NavStatus).toStdString()))));
          result.append(")");
        }
      } else if (Class == AIS_SART) {
        result.append(" (");
        if (NavStatus == RESERVED_14)
          result.append(FromWx(_("Active")));
        else if (NavStatus == UNDEFINED)
          result.append(FromWx(_("Testing")));
        result.append(")");
      } else if (Class == AIS_DSC) {
        result.append(" (");
        result.append(GetNatureofDistress(m_dscNature));
        result.append(")");
      }
    }
  }

  if (g_bAISRolloverShowCOG && ((SOG <= 102.2) || b_SarAircraftPosnReport) &&
      !((Class == AIS_ATON) || (Class == AIS_BASE) || (Class == AIS_METEO))) {
    if (result.length()) result += "\n";

    double speed_show = toUsrSpeed(SOG);
    if (speed_show < 10.0)
      result += QString::asprintf("SOG %.2f ", speed_show) +
                FromWx(getUsrSpeedUnit()) + " ";
    else if (speed_show < 100.0)
      result += QString::asprintf("SOG %.1f ", speed_show) +
                FromWx(getUsrSpeedUnit()) + " ";
    else
      result += QString::asprintf("SOG %.0f ", speed_show) +
                FromWx(getUsrSpeedUnit()) + " ";

    int crs = qRound(COG);
    if (b_positionOnceValid) {
      if (crs < 360) {
        QString magString, trueString;
        if (g_bShowMag)
          magString += QString::asprintf(
              "%03d%c(M)  ", static_cast<int>(m_callbacks.get_mag(COG)),
              0x00B0);
        if (g_bShowTrue)
          trueString += QString::asprintf("%03d%c ", (int)crs, 0x00B0);

        result += trueString + magString;
      }

      else if (COG == 360.0)
        result += FromWx(_(" COG Unavailable"));
      else if (crs == 360)
        result += QString(" COG 000\u00B0");
    } else
      result += FromWx(_(" COG Unavailable"));
  }

  if (g_bAISRolloverShowCPA && bCPA_Valid && Class != AIS_METEO) {
    if (result.length()) result += "\n";
    result += FromWx(_("CPA")) + " " + FromWx(FormatDistanceAdaptive(CPA)) +
              " " + FromWx(_("in")) + " " + QString::asprintf("%.0f", TCPA) +
              " " + FromWx(_("min"));
  }
  if (Class == AIS_METEO) {
    if (met_data.wind_kn < 122) {
      if (result.length()) result += "\n";
      double userwindspeed = toUsrWindSpeed(met_data.wind_kn);
      result += FromWx(_("Wind speed"));
      result += FromWx(wxString::Format(": %0.1f %s", userwindspeed,
                                        getUsrWindSpeedUnit())) +
                QString::asprintf(" %d%c ", met_data.wind_dir, 0x00B0);
    }

    if (met_data.water_lev_dev < 30.) {
      if (result.length()) result += "\n";
      result += FromWx(_("Water level deviation"));
      double userdepth;
      userdepth = toUsrDepth(met_data.water_lev_dev);
      result += FromWx(
          wxString::Format(": %.1f %s", userdepth, getUsrDepthUnit()));

    } else if (met_data.water_level > -32.) {
      if (result.length()) result += "\n";
      result += FromWx(_("Water level"));
      double userdepth;
      userdepth = toUsrDepth(met_data.water_level);
      result += FromWx(
          wxString::Format(": %.1f %s", userdepth, getUsrDepthUnit()));
    }

    if (met_data.current < 25.) {
      if (result.length()) result += "\n";
      result += FromWx(_("Current"));
      result += QString::asprintf(": %.1f ", met_data.current) +
                FromWx(_("kts")) +
                QString::asprintf(" %d%c ", met_data.curr_dir, 0x00B0);
    }

    if (met_data.wave_height < 24.6) {
      if (result.length()) result += "\n";
      double userwh = toUsrDepth(met_data.wave_height);
      result += FromWx(_("Wave height")) +
                FromWx(wxString::Format(": %.1f %s", userwh,
                                        getUsrDepthUnit())) +
                " / " + QString::number(met_data.wave_period) + " " +
                FromWx(_("s"));
    }

    if (met_data.water_temp < 50.) {
      if (result.length()) result += "\n";
      double usertemp = toUsrTemp(met_data.water_temp);
      result += FromWx(_("Water temp"));
      result += QString::asprintf(": %.1f%c", usertemp, 0x00B0) +
                FromWx(getUsrTempUnit());
    }

    if (met_data.air_temp != -102.4) {
      if (result.length()) result += "\n";
      double usertemp = toUsrTemp(met_data.air_temp);
      result += FromWx(_("Air temp"));
      result += QString::asprintf(": %.1f%c", usertemp, 0x00B0) +
                FromWx(getUsrTempUnit()) + " ";
    }

    if (met_data.airpress > 799 && met_data.airpress < 1310) {
      if (met_data.air_temp == -102.4 && result.length()) result += "\n";
      result += FromWx(_("Air press"));
      result += QString::asprintf(": %d hPa", met_data.airpress);
    }

    if (met_data.hor_vis < 12.) {
      if (result.length()) result += "\n";
      double userVisDist = toUsrDistance(met_data.hor_vis);
      QString horVis = FromWx(
          wxString::Format(": %s%.1f %s", (met_data.hor_vis_GT ? ">" : ""),
                           userVisDist, getUsrDistanceUnit()));
      result += FromWx(_("Visibility")) + horVis;
    }
  }
  return result;
}

QString AisTargetData::Get_vessel_type_string(bool b_short) {
  int i = 19;
  if (Class == AIS_ATON) {
    i = ShipType + 20;
  } else
    switch (ShipType) {
      case 30:
        i = 0;
        break;
      case 31:
        i = 1;
        break;
      case 32:
        i = 2;
        break;
      case 33:
        i = 3;
        break;
      case 34:
        i = 4;
        break;
      case 35:
        i = 5;
        break;
      case 36:
        i = 6;
        break;
      case 37:
        i = 7;
        break;
      case 50:
        i = 9;
        break;
      case 51:
        i = 10;
        break;
      case 52:
        i = 11;
        break;
      case 53:
        i = 12;
        break;
      case 54:
        i = 13;
        break;
      case 55:
        i = 14;
        break;
      case 58:
        i = 15;
        break;
      default:
        i = 19;
        break;
    }

  if ((Class == AIS_CLASS_B) || (Class == AIS_CLASS_A)) {
    if ((ShipType >= 40) && (ShipType < 50)) i = 8;

    if ((ShipType >= 60) && (ShipType < 70)) i = 16;

    if ((ShipType >= 70) && (ShipType < 80)) i = 17;

    if ((ShipType >= 80) && (ShipType < 90)) i = 18;
  } else if (Class == AIS_GPSG_BUDDY)
    i = 52;
  else if (Class == AIS_ARPA)
    i = 55;
  else if (Class == AIS_APRS)
    i = 56;
  else if (Class == AIS_BUOY)
    i = 57;
  else if (Class == AIS_DSC)
    i = (ShipType == 12 || ShipType == 16) ? 54 : 53;  // 12 & 16 is distress

  if (!b_short)
    return ais_get_type(i);
  else
    return ais_get_short_type(i);
}

QString AisTargetData::Get_class_string(bool b_short) {
  switch (Class) {
    case AIS_CLASS_A:
      return FromWx(_("A"));
    case AIS_CLASS_B:
      return FromWx(_("B"));
    case AIS_ATON:
      return FromWx(b_short ? _("AtoN") : _("Aid to Navigation"));
    case AIS_BASE:
      return FromWx(b_short ? _("Base") : _("Base Station"));
    case AIS_GPSG_BUDDY:
      return FromWx(b_short ? _("Buddy") : _("GPSGate Buddy"));
    case AIS_DSC:
      if (ShipType == 12 || (ShipType == 16 && m_dscNature < 13))
        return FromWx(b_short ? _("DSC") : _("DSC Distress"));
      else
        return FromWx(b_short ? _("DSC") : _("DSC Position Report"));
    case AIS_SART:
      return FromWx(b_short ? _("SART") : _("SART"));
    case AIS_ARPA:
      return FromWx(b_short ? _("ARPA") : _("ARPA"));
    case AIS_BUOY:
      return FromWx(b_short ? _("Buoy") : _("BUOY"));
    case AIS_APRS:
      return FromWx(b_short ? _("APRS") : _("APRS Position Report"));
    case AIS_METEO:
      return FromWx(b_short ? _("Meteo") : _("Meteorologic"));

    default:
      return FromWx(b_short ? _("Unk") : _("Unknown"));
  }
}

QString AisTargetData::GetNatureofDistress(int dscnature) {
  // Natures of distress from: Rec. ITU-R M.493-10.
  wxString dscDistressType[] = {_("Fire, explosion"),
                                _("Flooding"),
                                _("Collision"),
                                _("Grounding"),
                                _("Listing, in danger of capsizing"),
                                _("Sinking"),
                                _("Disabled and adrift"),
                                _("Undesignated distress"),
                                _("Abandoning ship"),
                                _("Piracy/armed robbery attack"),
                                _("Man overboard"),
                                "-",
                                _("EPIRB emission")};
  if (dscnature >= 0 && dscnature < 13)
    return FromWx(dscDistressType[dscnature]);

  return "";
}

void AisTargetData::Toggle_AIS_CPA() {
  b_show_AIS_CPA = !b_show_AIS_CPA ? true : false;
}

void AisTargetData::ToggleShowTrack() {
  b_show_track = !b_show_track ? true : false;
}

bool AisTargetData::IsValidMID(int mid) {
  if (mid >= 201 && mid <= 775) return true;
  return false;
}

// Get country name and code according to ITU 2023-02
// (http://www.itu.int/en/ITU-R/terrestrial/fmd/Pages/mid.aspx)
QString AisTargetData::GetCountryCode(bool b_CntryLongStr) {
  // The country lookup is computed as a wxString (the _() translation macro,
  // still in use pending P3.10) and converted to QString on return.
  auto computeWx = [&]() -> wxString {
  if (Class == AIS_BUOY || Class == AIS_ARPA) return "";
  /***** Check for a valid MID *****/
  // Meteo adaption
  int tmpMmsi = met_data.original_mmsi ? met_data.original_mmsi : MMSI;
  // First check the most common case
  int nMID = tmpMmsi / 1000000;
  if (!IsValidMID(nMID) || Class == AIS_ATON) {
    // SART, MOB, EPIRB starts with 97 and don't use MID (ITU-R M.1371-5)
    // or healthy check
    if (tmpMmsi < 1000 || 97 == tmpMmsi / 10000000) return "";

    // Find MID when not in first position like e.g. SAR/ATON
    wxString s_mmsi;
    s_mmsi << tmpMmsi;
    bool foundMID = false;
    size_t i;
    i = nMID > 900 ? 2 : 0;  // AIS_ATON or others where MMSI starts with 9x
    for (i; i < s_mmsi.length() - 3; i++) {
      nMID = wxAtoi(s_mmsi.Mid(i, 3));
      if (IsValidMID(nMID)) {
        foundMID = true;
        break;
      }
    }
    if (!foundMID) return "";
  }

#if wxUSE_XLOCALE || !wxCHECK_VERSION(3, 0, 0)

  switch (nMID) {
    case 201:
      return b_CntryLongStr ? _("Albania") : "AL";
    case 202:
      return b_CntryLongStr ? _("Andorra") : "AD";
    case 203:
      return b_CntryLongStr ? _("Austria") : "AT";
    case 204:
      return b_CntryLongStr ? _("Azores") : "AZ";
    case 205:
      return b_CntryLongStr ? _("Belgium") : "BE";
    case 206:
      return b_CntryLongStr ? _("Belarus") : "BY";
    case 207:
      return b_CntryLongStr ? _("Bulgaria") : "BG";
    case 208:
      return b_CntryLongStr ? _("Vatican City State") : "VA";
    case 209:
    case 210:
      return b_CntryLongStr ? _("Cyprus") : "CY";
    case 211:
      return b_CntryLongStr ? _("Germany") : "DE";
    case 212:
      return b_CntryLongStr ? _("Cyprus") : "CY";
    case 213:
      return b_CntryLongStr ? _("Georgia") : "GE";
    case 214:
      return b_CntryLongStr ? _("Moldova") : "MD";
    case 215:
      return b_CntryLongStr ? _("Malta") : "MT";
    case 216:
      return b_CntryLongStr ? _("Armenia") : "AM";
    case 218:
      return b_CntryLongStr ? _("Germany") : "DE";
    case 219:
    case 220:
      return b_CntryLongStr ? _("Denmark") : "DK";
    case 224:
      return b_CntryLongStr ? _("Spain") : "ES";
    case 225:
      return b_CntryLongStr ? _("Spain") : "ES";
    case 226:
    case 227:
    case 228:
      return b_CntryLongStr ? _("France") : "FR";
    case 229:
      return b_CntryLongStr ? _("Malta") : "MT";
    case 230:
      return b_CntryLongStr ? _("Finland") : "FI";
    case 231:
      return b_CntryLongStr ? _("Faroe Islands") : "FO";
    case 232:
    case 233:
    case 234:
    case 235:
      return b_CntryLongStr ? _("Great Britain") : "GB";
    case 236:
      return b_CntryLongStr ? _("Gibraltar") : "GI";
    case 237:
      return b_CntryLongStr ? _("Greece") : "GR";
    case 238:
      return b_CntryLongStr ? _("Croatia") : "HR";
    case 239:
    case 240:
    case 241:
      return b_CntryLongStr ? _("Greece") : "GR";
    case 242:
      return b_CntryLongStr ? _("Morocco") : "MA";
    case 243:
      return b_CntryLongStr ? _("Hungary") : "HU";
    case 244:
    case 245:
    case 246:
      return b_CntryLongStr ? _("Netherlands") : "NL";
    case 247:
      return b_CntryLongStr ? _("Italy") : "IT";
    case 248:
    case 249:
      return b_CntryLongStr ? _("Malta") : "MT";
    case 250:
      return b_CntryLongStr ? _("Ireland") : "IE";
    case 251:
      return b_CntryLongStr ? _("Iceland") : "IS";
    case 252:
      return b_CntryLongStr ? _("Liechtenstein") : "LI";
    case 253:
      return b_CntryLongStr ? _("Luxembourg") : "LU";
    case 254:
      return b_CntryLongStr ? _("Monaco") : "MC";
    case 255:
      return b_CntryLongStr ? _("Madeira") : "PT";
    case 256:
      return b_CntryLongStr ? _("Malta") : "MT";
    case 257:
    case 258:
    case 259:
      return b_CntryLongStr ? _("Norway") : "NO";
    case 261:
      return b_CntryLongStr ? _("Poland") : "PL";
    case 262:
      return b_CntryLongStr ? _("Montenegro") : "ME";
    case 263:
      return b_CntryLongStr ? _("Portugal") : "PT";
    case 264:
      return b_CntryLongStr ? _("Romania") : "RO";
    case 265:
    case 266:
      return b_CntryLongStr ? _("Sweden") : "SE";
    case 267:
      return b_CntryLongStr ? _("Slovak Republic") : "SK";
    case 268:
      return b_CntryLongStr ? _("San Marino") : "SM";
    case 269:
      return b_CntryLongStr ? _("Switzerland") : "CH";
    case 270:
      return b_CntryLongStr ? _("Czech Republic") : "CZ";
    case 271:
      return b_CntryLongStr ? _("Turkey") : "TR";
    case 272:
      return b_CntryLongStr ? _("Ukraine") : "UA";
    case 273:
      return b_CntryLongStr ? _("Russian") : "RU";
    case 274:
      return b_CntryLongStr ? _("Macedonia") : "MK";
    case 275:
      return b_CntryLongStr ? _("Latvia") : "LV";
    case 276:
      return b_CntryLongStr ? _("Estonia") : "EE";
    case 277:
      return b_CntryLongStr ? _("Lithuania") : "LT";
    case 278:
      return b_CntryLongStr ? _("Slovenia") : "SI";
    case 279:
      return b_CntryLongStr ? _("Serbia") : "RS";
    case 301:
      return b_CntryLongStr ? _("Anguilla") : "AI";
    case 303:
      return b_CntryLongStr ? _("Alaska") : "AK";
    case 304:
    case 305:
      return b_CntryLongStr ? _("Antigua and Barbuda") : "AG";
    case 306:
      return b_CntryLongStr ? _("Antilles") : "AN";
    case 307:
      return b_CntryLongStr ? _("Aruba") : "AW";
    case 308:
    case 309:
      return b_CntryLongStr ? _("Bahamas") : "BS";
    case 310:
      return b_CntryLongStr ? _("Bermuda") : "BM";
    case 311:
      return b_CntryLongStr ? _("Bahamas") : "BS";
    case 312:
      return b_CntryLongStr ? _("Belize") : "BZ";
    case 314:
      return b_CntryLongStr ? _("Barbados") : "BB";
    case 316:
      return b_CntryLongStr ? _("Canada") : "CA";
    case 319:
      return b_CntryLongStr ? _("Cayman Islands") : "KY";
    case 321:
      return b_CntryLongStr ? _("Costa Rica") : "CR";
    case 323:
      return b_CntryLongStr ? _("Cuba") : "CU";
    case 325:
      return b_CntryLongStr ? _("Dominica") : "DM";
    case 327:
      return b_CntryLongStr ? _("Dominican Republic") : "DM";
    case 329:
      return b_CntryLongStr ? _("Guadeloupe") : "GP";
    case 330:
      return b_CntryLongStr ? _("Grenada") : "GD";
    case 331:
      return b_CntryLongStr ? _("Greenland") : "GL";
    case 332:
      return b_CntryLongStr ? _("Guatemala") : "GT";
    case 334:
      return b_CntryLongStr ? _("Honduras") : "HN";
    case 336:
      return b_CntryLongStr ? _("Haiti") : "HT";
    case 338:
      return b_CntryLongStr ? _("United States of America") : "US";
    case 339:
      return b_CntryLongStr ? _("Jamaica") : "JM";
    case 341:
      return b_CntryLongStr ? _("Saint Kitts and Nevis") : "KN";
    case 343:
      return b_CntryLongStr ? _("Saint Lucia") : "LC";
    case 345:
      return b_CntryLongStr ? _("Mexico") : "MX";
    case 347:
      return b_CntryLongStr ? _("Martinique") : "MQ";
    case 348:
      return b_CntryLongStr ? _("Montserrat") : "MS";
    case 350:
      return b_CntryLongStr ? _("Nicaragua") : "NI";
    case 351:
    case 352:
    case 353:
    case 354:
    case 355:
    case 356:
    case 357:
      return b_CntryLongStr ? _("Panama") : "PA";
    case 358:
      return b_CntryLongStr ? _("Puerto Rico") : "PR";
    case 359:
      return b_CntryLongStr ? _("El Salvador") : "SV";
    case 361:
      return b_CntryLongStr ? _("Saint Pierre and Miquelon") : "PM";
    case 362:
      return b_CntryLongStr ? _("Trinidad and Tobago") : "TT";
    case 364:
      return b_CntryLongStr ? _("Turks and Caicos Islands") : "TC";
    case 366:
    case 367:
    case 368:
    case 369:
      return b_CntryLongStr ? _("United States of America") : "US";
    case 370:
    case 371:
    case 372:
    case 373:
    case 374:
      return b_CntryLongStr ? _("Panama") : "PA";
    case 375:
    case 376:
    case 377:
      return b_CntryLongStr ? _("Saint Vincent and the Grenadines") : "VC";
    case 378:
      return b_CntryLongStr ? _("British Virgin Islands") : "VG";
    case 379:
      return b_CntryLongStr ? _("United States Virgin Islands") : "AE";
    case 401:
      return b_CntryLongStr ? _("Afghanistan") : "AF";
    case 403:
      return b_CntryLongStr ? _("Saudi Arabia") : "SA";
    case 405:
      return b_CntryLongStr ? _("Bangladesh") : "BD";
    case 408:
      return b_CntryLongStr ? _("Bahrain") : "BH";
    case 410:
      return b_CntryLongStr ? _("Bhutan") : "BT";
    case 412:
    case 413:
    case 414:
      return b_CntryLongStr ? _("China") : "CN";
    case 416:
      return b_CntryLongStr ? _("Taiwan") : "TW";
    case 417:
      return b_CntryLongStr ? _("Sri Lanka") : "LK";
    case 419:
      return b_CntryLongStr ? _("India") : "IN";
    case 422:
      return b_CntryLongStr ? _("Iran") : "IR";
    case 423:
      return b_CntryLongStr ? _("Azerbaijani Republic") : "AZ";
    case 425:
      return b_CntryLongStr ? _("Iraq") : "IQ";
    case 428:
      return b_CntryLongStr ? _("Israel") : "IL";
    case 431:
      return b_CntryLongStr ? _("Japan") : "JP";
    case 432:
      return b_CntryLongStr ? _("Japan") : "JP";
    case 434:
      return b_CntryLongStr ? _("Turkmenistan") : "TM";
    case 436:
      return b_CntryLongStr ? _("Kazakhstan") : "KZ";
    case 437:
      return b_CntryLongStr ? _("Uzbekistan") : "UZ";
    case 438:
      return b_CntryLongStr ? _("Jordan") : "JO";
    case 440:
    case 441:
      return b_CntryLongStr ? _("Korea") : "KR";
    case 443:
      return b_CntryLongStr ? _("Palestine") : "PS";
    case 445:
      return b_CntryLongStr ? _("People's Rep. of Korea") : "KP";
    case 447:
      return b_CntryLongStr ? _("Kuwait") : "KW";
    case 450:
      return b_CntryLongStr ? _("Lebanon") : "LB";
    case 451:
      return b_CntryLongStr ? _("Kyrgyz Republic") : "KG";
    case 453:
      return b_CntryLongStr ? _("Macao") : "MO";
    case 455:
      return b_CntryLongStr ? _("Maldives") : "MV";
    case 457:
      return b_CntryLongStr ? _("Mongolia") : "MN";
    case 459:
      return b_CntryLongStr ? _("Nepal") : "NP";
    case 461:
      return b_CntryLongStr ? _("Oman") : "OM";
    case 463:
      return b_CntryLongStr ? _("Pakistan") : "PK";
    case 466:
      return b_CntryLongStr ? _("Qatar") : "QA";
    case 468:
      return b_CntryLongStr ? _("Syrian Arab Republic") : "SY";
    case 470:
    case 471:
      return b_CntryLongStr ? _("United Arab Emirates") : "AE";
    case 472:
      return b_CntryLongStr ? _("Tajikistan") : "TJ";
    case 473:
    case 475:
      return b_CntryLongStr ? _("Yemen") : "YE";
    case 477:
      return b_CntryLongStr ? _("Hong Kong") : "HK";
    case 478:
      return b_CntryLongStr ? _("Bosnia and Herzegovina") : "BA";
    case 501:
      return b_CntryLongStr ? _("Adelie Land") : "TF";
    case 503:
      return b_CntryLongStr ? _("Australia") : "AU";
    case 506:
      return b_CntryLongStr ? _("Myanmar") : "MM";
    case 508:
      return b_CntryLongStr ? _("Brunei Darussalam") : "BN";
    case 510:
      return b_CntryLongStr ? _("Micronesia") : "FM";
    case 511:
      return b_CntryLongStr ? _("Palau") : "PW";
    case 512:
      return b_CntryLongStr ? _("New Zealand") : "NZ";
    case 514:
    case 515:
      return b_CntryLongStr ? _("Cambodia") : "KH";
    case 516:
      return b_CntryLongStr ? _("Christmas Island") : "CX";
    case 518:
      return b_CntryLongStr ? _("Cook Islands") : "CK";
    case 520:
      return b_CntryLongStr ? _("Fiji") : "FJ";
    case 523:
      return b_CntryLongStr ? _("Cocos (Keeling) Islands") : "CC";
    case 525:
      return b_CntryLongStr ? _("Indonesia") : "ID";
    case 529:
      return b_CntryLongStr ? _("Kiribati") : "KI";
    case 531:
      return b_CntryLongStr ? _("Lao People's Dem. Rep.") : "LA";
    case 533:
      return b_CntryLongStr ? _("Malaysia") : "MY";
    case 536:
      return b_CntryLongStr ? _("Northern Mariana Islands") : "MP";
    case 538:
      return b_CntryLongStr ? _("Marshall Islands") : "MH";
    case 540:
      return b_CntryLongStr ? _("New Caledonia") : "NC";
    case 542:
      return b_CntryLongStr ? _("Niue") : "NU";
    case 544:
      return b_CntryLongStr ? _("Nauru") : "NR";
    case 546:
      return b_CntryLongStr ? _("French Polynesia") : "PF";
    case 548:
      return b_CntryLongStr ? _("Philippines") : "PH";
    case 550:
      return b_CntryLongStr ? _("East Timor") : "TL";
    case 553:
      return b_CntryLongStr ? _("Papua New Guinea") : "PG";
    case 555:
      return b_CntryLongStr ? _("Pitcairn Island") : "PN";
    case 557:
      return b_CntryLongStr ? _("Solomon Islands") : "SB";
    case 559:
      return b_CntryLongStr ? _("American Samoa") : "AS";
    case 561:
      return b_CntryLongStr ? _("Samoa") : "WS";
    case 563:
    case 564:
    case 565:
    case 566:
      return b_CntryLongStr ? _("Singapore") : "SG";
    case 567:
      return b_CntryLongStr ? _("Thailand") : "TH";
    case 570:
      return b_CntryLongStr ? _("Tonga") : "TO";
    case 572:
      return b_CntryLongStr ? _("Tuvalu") : "TV";
    case 574:
      return b_CntryLongStr ? _("Viet Nam") : "VN";
    case 576:
    case 577:
      return b_CntryLongStr ? _("Vanuatu") : "VU";
    case 578:
      return b_CntryLongStr ? _("Wallis and Futuna Islands") : "WF";
    case 601:
      return b_CntryLongStr ? _("South Africa") : "ZA";
    case 603:
      return b_CntryLongStr ? _("Angola") : "AO";
    case 605:
      return b_CntryLongStr ? _("Algeria") : "DZ";
    case 607:
      return b_CntryLongStr ? _("Saint Paul") : "TF";
    case 608:
      return b_CntryLongStr ? _("Ascension Island") : "SH";
    case 609:
      return b_CntryLongStr ? _("Burundi") : "BI";
    case 610:
      return b_CntryLongStr ? _("Benin") : "BJ";
    case 611:
      return b_CntryLongStr ? _("Botswana") : "BW";
    case 612:
      return b_CntryLongStr ? _("Central African Republic") : "CF";
    case 613:
      return b_CntryLongStr ? _("Cameroon") : "CM";
    case 615:
      return b_CntryLongStr ? _("Congo") : "CD";
    case 616:
      return b_CntryLongStr ? _("Comoros") : "KM";
    case 617:
      return b_CntryLongStr ? _("Capo Verde") : "CV";
    case 618:
      return b_CntryLongStr ? _("Crozet Archipelago") : "TF";
    case 619:
      return b_CntryLongStr ? _("Ivory Coast") : "CI";
    case 620:
      return b_CntryLongStr ? _("Comoros (Union of the)") : "KM";
    case 621:
      return b_CntryLongStr ? _("Djibouti") : "DJ";
    case 622:
      return b_CntryLongStr ? _("Egypt") : "EG";
    case 624:
      return b_CntryLongStr ? _("Ethiopia") : "ET";
    case 625:
      return b_CntryLongStr ? _("Eritrea") : "ER";
    case 626:
      return b_CntryLongStr ? _("Gabonese Republic") : "GA";
    case 627:
      return b_CntryLongStr ? _("Ghana") : "GH";
    case 629:
      return b_CntryLongStr ? _("Gambia") : "GM";
    case 630:
      return b_CntryLongStr ? _("Guinea-Bissau") : "GW";
    case 631:
      return b_CntryLongStr ? _("Equatorial Guinea") : "GQ";
    case 632:
      return b_CntryLongStr ? _("Guinea") : "GN";
    case 633:
      return b_CntryLongStr ? _("Burkina Faso") : "BF";
    case 634:
      return b_CntryLongStr ? _("Kenya") : "KE";
    case 635:
      return b_CntryLongStr ? _("Kerguelen Islands") : "TF";
    case 636:
    case 637:
      return b_CntryLongStr ? _("Liberia") : "LR";
    case 638:
      return b_CntryLongStr ? _("South Sudan (Republic of)") : "SS";
    case 642:
      return b_CntryLongStr ? _("Libya") : "LY";
    case 644:
      return b_CntryLongStr ? _("Lesotho") : "LS";
    case 645:
      return b_CntryLongStr ? _("Mauritius") : "MU";
    case 647:
      return b_CntryLongStr ? _("Madagascar") : "MG";
    case 649:
      return b_CntryLongStr ? _("Mali") : "ML";
    case 650:
      return b_CntryLongStr ? _("Mozambique") : "MZ";
    case 654:
      return b_CntryLongStr ? _("Mauritania") : "MR";
    case 655:
      return b_CntryLongStr ? _("Malawi") : "MW";
    case 656:
      return b_CntryLongStr ? _("Niger") : "NE";
    case 657:
      return b_CntryLongStr ? _("Nigeria") : "NG";
    case 659:
      return b_CntryLongStr ? _("Namibia") : "NA";
    case 660:
      return b_CntryLongStr ? _("Reunion") : "RE";
    case 661:
      return b_CntryLongStr ? _("Rwanda") : "RW";
    case 662:
      return b_CntryLongStr ? _("Sudan") : "SD";
    case 663:
      return b_CntryLongStr ? _("Senegal") : "SN";
    case 664:
      return b_CntryLongStr ? _("Seychelles") : "SC";
    case 665:
      return b_CntryLongStr ? _("Saint Helena") : "SH";
    case 666:
      return b_CntryLongStr ? _("Somalia") : "SO";
    case 667:
      return b_CntryLongStr ? _("Sierra Leone") : "SL";
    case 668:
      return b_CntryLongStr ? _("Sao Tome and Principe") : "ST";
    case 669:
      return b_CntryLongStr ? _("Eswatini") : "SZ";
    case 670:
      return b_CntryLongStr ? _("Chad") : "TD";
    case 671:
      return b_CntryLongStr ? _("Togolese Republic") : "TG";
    case 672:
      return b_CntryLongStr ? _("Tunisia") : "TN";
    case 674:
      return b_CntryLongStr ? _("Tanzania") : "TZ";
    case 675:
      return b_CntryLongStr ? _("Uganda") : "UG";
    case 676:
      return b_CntryLongStr ? _("Dem Rep.of the Congo") : "CD";
    case 677:
      return b_CntryLongStr ? _("Tanzania") : "TZ";
    case 678:
      return b_CntryLongStr ? _("Zambia") : "ZM";
    case 679:
      return b_CntryLongStr ? _("Zimbabwe") : "ZW";
    case 701:
      return b_CntryLongStr ? _("Argentine Republic") : "AR";
    case 710:
      return b_CntryLongStr ? _("Brazil") : "BR";
    case 720:
      return b_CntryLongStr ? _("Bolivia") : "BO";
    case 725:
      return b_CntryLongStr ? _("Chile") : "CL";
    case 730:
      return b_CntryLongStr ? _("Colombia") : "CO";
    case 735:
      return b_CntryLongStr ? _("Ecuador") : "EC";
    case 740:
      return b_CntryLongStr ? _("Falkland Islands") : "FK";
    case 745:
      return b_CntryLongStr ? _("France - Guiana") : "GY";
    case 750:
      return b_CntryLongStr ? _("Guyana") : "GY";
    case 755:
      return b_CntryLongStr ? _("Paraguay") : "PY";
    case 760:
      return b_CntryLongStr ? _("Peru") : "PE";
    case 765:
      return b_CntryLongStr ? _("Suriname") : "SR";
    case 770:
      return b_CntryLongStr ? _("Uruguay") : "UY";
    case 775:
      return b_CntryLongStr ? _("Venezuela") : "VE";

    default:
      return "";
  }
#else
  return "";
#endif
  };  // computeWx
  return FromWx(computeWx());
}

QString ais_get_type(int index) {
  static const wxString ais_type[] = {
      _("Fishing Vessel"),                            // 30        0
      _("Towing Vessel"),                             // 31        1
      _("Towing Vessel, Long"),                       // 32        2
      _("Dredger"),                                   // 33        3
      _("Diving Ops Vessel"),                         // 34        4
      _("Military Vessel"),                           // 35        5
      _("Sailing Vessel"),                            // 36        6
      _("Pleasure craft"),                            // 37        7
      _("High Speed Craft"),                          // 4x        8
      _("Pilot Vessel"),                              // 50        9
      _("Search and Rescue Vessel"),                  // 51        10
      _("Tug"),                                       // 52        11
      _("Port Tender"),                               // 53        12
      _("Pollution Control Vessel"),                  // 54        13
      _("Law Enforcement Vessel"),                    // 55        14
      _("Medical Transport"),                         // 58        15
      _("Passenger Ship"),                            // 6x        16
      _("Cargo Ship"),                                // 7x        17
      _("Tanker"),                                    // 8x        18
      _("Unknown"),                                   //          19
      _("Unspecified"),                               // 00        20
      _("Reference Point"),                           // 01        21
      _("RACON"),                                     // 02        22
      _("Fixed Structure"),                           // 03        23
      _("Spare"),                                     // 04        24
      _("Light"),                                     // 05        25
      _("Light w/Sectors"),                           // 06        26
      _("Leading Light Front"),                       // 07        27
      _("Leading Light Rear"),                        // 08        28
      _("Cardinal N Beacon"),                         // 09        29
      _("Cardinal E Beacon"),                         // 10        30
      _("Cardinal S Beacon"),                         // 11        31
      _("Cardinal W Beacon"),                         // 12        32
      _("Beacon, Port Hand"),                         // 13        33
      _("Beacon, Starboard Hand"),                    // 14        34
      _("Beacon, Preferred Channel Port Hand"),       // 15        35
      _("Beacon, Preferred Channel Starboard Hand"),  // 16        36
      _("Beacon, Isolated Danger"),                   // 17        37
      _("Beacon, Safe Water"),                        // 18        38
      _("Beacon, Special Mark"),                      // 19        39
      _("Cardinal Mark N"),                           // 20        40
      _("Cardinal Mark E"),                           // 21        41
      _("Cardinal Mark S"),                           // 22        42
      _("Cardinal Mark W"),                           // 23        43
      _("Port Hand Mark"),                            // 24        44
      _("Starboard Hand Mark"),                       // 25        45
      _("Preferred Channel Port Hand"),               // 26        46
      _("Preferred Channel Starboard Hand"),          // 27        47
      _("Isolated Danger"),                           // 28        48
      _("Safe Water"),                                // 29        49
      _("Special Mark"),                              // 30        50
      _("Light Vessel/Rig"),                          // 31        51
      _("GpsGate Buddy"),                             // xx        52
      _("Position Report"),                           // xx        53
      _("Distress"),                                  // xx        54
      _("ARPA radar target"),                         // xx        55
      _("APRS Position Report"),                      // xx        56
      _("Buoy or similar")                            // xx        57
  };

  return FromWx(ais_type[index]);
}

QString ais_get_short_type(int index) {
  static const wxString short_ais_type[] = {
      _("F/V"),       // 30        0
      _("Tow"),       // 31        1
      _("Long Tow"),  // 32        2
      _("Dredge"),    // 33        3
      _("D/V"),       // 34        4
      _("Mil/V"),     // 35        5
      _("S/V"),       // 36        6
      _("Yat"),       // 37        7
      _("HSC"),       // 4x        8
      _("P/V"),       // 50        9
      _("SAR/V"),     // 51        10
      _("Tug"),       // 52        11
      _("Tender"),    // 53        12
      _("PC/V"),      // 54        13
      _("LE/V"),      // 55        14
      _("Med/V"),     // 58        15
      _("Pass/V"),    // 6x        16
      _("M/V"),       // 7x        17
      _("M/T"),       // 8x        18
      _("?"),         //          19

      _("AtoN"),            // 00        20
      _("Ref. Pt"),         // 01        21
      _("RACON"),           // 02        22
      _("Fix.Struct."),     // 03        23
      _("?"),               // 04        24
      _("Lt"),              // 05        25
      _("Lt sect."),        // 06        26
      _("Ldg Lt Front"),    // 07        27
      _("Ldg Lt Rear"),     // 08        28
      _("Card. N"),         // 09        29
      _("Card. E"),         // 10        30
      _("Card. S"),         // 11        31
      _("Card. W"),         // 12        32
      _("Port"),            // 13        33
      _("Stbd"),            // 14        34
      _("Pref. Chnl"),      // 15        35
      _("Pref. Chnl"),      // 16        36
      _("Isol. Dngr"),      // 17        37
      _("Safe Water"),      // 18        38
      _("Special"),         // 19        39
      _("Card. N"),         // 20        40
      _("Card. E"),         // 21        41
      _("Card. S"),         // 22        42
      _("Card. W"),         // 23        43
      _("Port Hand"),       // 24        44
      _("Stbd Hand"),       // 25        45
      _("Pref. Chnl"),      // 26        46
      _("Pref. Chnl"),      // 27        47
      _("Isol. Dngr"),      // 28        48
      _("Safe Water"),      // 29        49
      _("Special"),         // 30        50
      _("LtV/Rig"),         // 31        51
      _("Buddy"),           // xx        52
      _("DSC"),             // xx        53
      _("Distress"),        // xx        54
      _("ARPA"),            // xx        55
      _("APRS"),            // xx        56
      _("Buoy or similar")  // xx        57
  };
  return FromWx(short_ais_type[index]);
}
