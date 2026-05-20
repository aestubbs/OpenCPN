/***************************************************************************
 *   Copyright (C) 2019 by David S. Register                               *
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
 * Time textbox to replace broken wxTimePickerCtrl on wxGTK
 *
 * This class is a drop-in for wxTimePickerCtrl on platforms where the latter
 * is unreliable, so its public surface intentionally mirrors wxTimePickerCtrl
 * (takes/returns wxDateTime). Internally the parsing/formatting is done with
 * QTime, with the wxDateTime kept only as the public payload for the
 * wxEVT_TIME_CHANGED event. The wxDateTime surface here will go away alongside
 * wxDatePickerCtrl in P1.10.
 */

#ifndef time_textbox_h
#define time_textbox_h

#pragma once

#include <QDateTime>

#include <wx/dateevt.h>
#include <wx/datetime.h>
#include <wx/msgdlg.h>
#include <wx/textctrl.h>

#include "model/wx_qt_string.h"

#define NO_TIME "00:00"

class TimeCtrl : public wxTextCtrl {
public:
  TimeCtrl(wxWindow *parent, wxWindowID id,
           const wxDateTime &value = wxDefaultDateTime,
           const wxPoint &pos = wxDefaultPosition,
           const wxSize &size = wxDefaultSize, long style = 0,
           const wxValidator &validator = wxDefaultValidator,
           const wxString &name = wxTextCtrlNameStr)
      : wxTextCtrl(parent, id, FormatInitial(value), pos, size, style,
                   validator, name) {
    Bind(wxEVT_KEY_UP, &TimeCtrl::OnChar, this);
    Bind(wxEVT_KILL_FOCUS, &TimeCtrl::OnKillFocus, this);
  };

  void SetValue(const wxDateTime &val) {
    if (val.IsValid()) {
      QTime t(val.GetHour(), val.GetMinute(), val.GetSecond());
      wxTextCtrl::SetValue(QString_to_wxString(t.toString("HH:mm")));
    } else {
      wxTextCtrl::SetValue(NO_TIME);
    }
  };

  wxDateTime GetValue() {
    QString str = wxString_to_QString(wxTextCtrl::GetValue());
    QTime t = QTime::fromString(str, "HH:mm");
    if (!t.isValid()) t = QTime::fromString(str, "H:mm");
    if (!t.isValid()) return wxInvalidDateTime;
    // wxTimePickerCtrl returns today's date with the parsed time of day.
    QDate today = QDate::currentDate();
    return wxDateTime(static_cast<wxDateTime::wxDateTime_t>(today.day()),
                      static_cast<wxDateTime::Month>(today.month() - 1),
                      today.year(),
                      static_cast<wxDateTime::wxDateTime_t>(t.hour()),
                      static_cast<wxDateTime::wxDateTime_t>(t.minute()),
                      static_cast<wxDateTime::wxDateTime_t>(t.second()));
  };

  void OnChar(wxKeyEvent &event) {
    wxDateTime v = GetValue();
    if (v.IsValid()) {
      wxDateEvent evt(this, v, wxEVT_TIME_CHANGED);
      HandleWindowEvent(evt);
    }
  };

  void OnKillFocus(wxFocusEvent &event) {
    wxDateTime v = GetValue();
    if (v.IsValid()) {
      QTime t(v.GetHour(), v.GetMinute(), v.GetSecond());
      wxTextCtrl::SetValue(QString_to_wxString(t.toString("HH:mm")));
    }
  };

  bool GetTime(int *hour, int *min, int *sec) {
    wxDateTime v = GetValue();
    if (!v.IsValid()) {
      *hour = *min = *sec = 0;
      return false;
    }
    *hour = v.GetHour();
    *min = v.GetMinute();
    *sec = v.GetSecond();
    return true;
  }

private:
  static wxString FormatInitial(const wxDateTime &value) {
    if (!value.IsValid()) return NO_TIME;
    QTime t(value.GetHour(), value.GetMinute(), value.GetSecond());
    return QString_to_wxString(t.toString("HH:mm"));
  }
};

#endif /* time_textbox_h */
