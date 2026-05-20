/***************************************************************************
 *   Copyright (C) 2025 by David S. Register                               *
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
 * Implement notification_manager.h -- User notifications manager
 */

#include <cmath>
#include <fstream>

#include <QDateTime>

#include "model/wx_qt_string.h"
#include <memory>
#include <sstream>
#include <vector>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include "model/base_platform.h"
#include "model/comm_appmsg_bus.h"
#include "model/datetime.h"
#include "model/navutil_base.h"
#include "model/notification.h"
#include "model/notification_manager.h"

NotificationManager& NotificationManager::GetInstance() {
  static NotificationManager instance;
  return instance;
}

NotificationManager::NotificationManager() {
  m_timeout_timer.Bind(wxEVT_TIMER, &NotificationManager::OnTimer, this,
                       m_timeout_timer.GetId());
  m_timeout_timer.Start(1000, wxTIMER_CONTINUOUS);
}

void NotificationManager::OnTimer(wxTimerEvent& event) {
  for (auto note : active_notifications) {
    if (note->GetTimeoutLeft() > 0) {
      note->DecrementTimoutCount();
    }
  }
  for (auto note : active_notifications) {
    if (note->GetTimeoutLeft() == 0) {
      AcknowledgeNotification(note->GetGuid());
      note->DecrementTimoutCount();
      break;
    }
  }
}

void NotificationManager::ScrubNotificationDirectory(int days_to_retain) {
  if (g_disableNotifications) return;
  QString note_directory =
      wxString_to_QString(g_BasePlatform->GetPrivateDataDir()) +
      QDir::separator() + "notifications" + QDir::separator();
  if (!QDir(note_directory).exists()) return;

  QDateTime now = QDateTime::currentDateTime();
  QDirIterator it(note_directory, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    QString path = it.next();
    QFileInfo fi(path);
    QDateTime mtime = fi.lastModified();
    qint64 age_secs = mtime.secsTo(now);  // seconds
    qint64 retain_secs = static_cast<qint64>(days_to_retain) * 24 * 3600;
    if (age_secs > retain_secs) {
      QFile::remove(path);
    }
  }
}

void NotificationManager::PersistNotificationAsFile(
    const std::shared_ptr<Notification> _notification) {
  QString note_directory =
      wxString_to_QString(g_BasePlatform->GetPrivateDataDir()) +
      QDir::separator() + "notifications" + QDir::separator();
  if (!QDir(note_directory).exists()) QDir().mkpath(note_directory);
  QString severity_prefix = "Info_";
  NotificationSeverity severity = _notification->GetSeverity();
  if (severity == NotificationSeverity::kWarning)
    severity_prefix = "Warning_";
  else if (severity == NotificationSeverity::kCritical)
    severity_prefix = "Critical_";
  QString file_name = note_directory + severity_prefix +
                      QString::fromStdString(_notification->GetGuid()) +
                      ".txt";

  QDateTime act_time_q = QDateTime::fromSecsSinceEpoch(
      static_cast<qint64>(_notification->GetActivateTime()), Qt::UTC);
  wxString stime = wxString::Format(
      "%s",
      QString_to_wxString(ocpn::toUsrDateTimeFormat(
          act_time_q, DateTimeFormatOptions().SetFormatString(
                          "$short_date  $24_hour_minutes_seconds"))));

  std::stringstream ss;
  ss << stime.ToStdString() << std::endl;
  ss << _notification->GetMessage() << std::endl;

  std::ofstream outputFile(file_name.toStdString().c_str(), std::ios::out);
  if (outputFile.is_open()) {
    outputFile << ss.str();
  }
}

NotificationSeverity NotificationManager::GetMaxSeverity() {
  int rv = 0;
  for (auto note : active_notifications) {
    int severity = static_cast<int>(note->GetSeverity());
    if (severity > rv) rv = severity;
  }
  return static_cast<NotificationSeverity>(rv);
}

std::string NotificationManager::AddNotification(
    std::shared_ptr<Notification> _notification) {
  if (g_disableNotifications) return "";

  active_notifications.push_back(_notification);
  PersistNotificationAsFile(_notification);
  evt_notificationlist_change.Notify();

  // Send notification to listeners
  auto msg = std::make_shared<NotificationMsg>("POST", _notification);
  AppMsgBus::GetInstance().Notify(std::move(msg));

  return _notification->GetGuid();
}

std::string NotificationManager::AddNotification(NotificationSeverity _severity,
                                                 const std::string& _message,
                                                 int _timeout_secs) {
  auto notification =
      std::make_shared<Notification>(_severity, _message, _timeout_secs);
  return AddNotification(notification);
}

bool NotificationManager::AcknowledgeNotification(const std::string& GUID) {
  if (!GUID.length()) return false;

  size_t target_message_hash = 0;
  for (auto it = active_notifications.begin();
       it != active_notifications.end();) {
    if ((*it)->GetGuid() == GUID) {
      target_message_hash = (*it)->GetStringHash();
      break;
    } else
      ++it;
  }
  if (!target_message_hash) return false;

  // erase multiple notifications with identical message_hash
  bool rv = false;
  bool done = false;
  while (!done && active_notifications.size()) {
    for (auto it = active_notifications.begin();
         it != active_notifications.end();) {
      if ((*it)->GetStringHash() == target_message_hash) {
        // Send notification to listeners
        auto msg = std::make_shared<NotificationMsg>("ACK", *it);
        AppMsgBus::GetInstance().Notify(std::move(msg));

        //  Drop the notification
        active_notifications.erase(it);

        rv = true;
        break;
      } else
        ++it;
      if (it == active_notifications.end()) done = true;
    }
  }

  if (rv) {
    evt_notificationlist_change.Notify();
  }

  return rv;
}

bool NotificationManager::AcknowledgeAllNotifications() {
  bool rv = false;

  while (active_notifications.size()) {
    AcknowledgeNotification(active_notifications[0]->GetGuid());
    rv = true;
  }

  return rv;
}
