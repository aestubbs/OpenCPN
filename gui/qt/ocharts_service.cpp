/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

/**
 * \file
 *
 * Implement ocharts_service.h.
 */

#include "ocharts_service.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#ifndef OCPN_QT_OEXSERVERD
#define OCPN_QT_OEXSERVERD ""
#endif

namespace ocpn::qtui {

OChartsService& OChartsService::instance() {
  static OChartsService s;
  return s;
}

OChartsService::OChartsService() { probeVersion(); }

QString OChartsService::daemonPath() const {
  const QString vendored = QString::fromUtf8(OCPN_QT_OEXSERVERD);
  if (!vendored.isEmpty() && QFileInfo::exists(vendored)) return vendored;
  // Fall back to an installed o-charts_pi copy (macOS layout).
  const QString installed =
      QDir::homePath() +
      QStringLiteral(
          "/Library/Application Support/OpenCPN/Contents/PlugIns/oexserverd");
  if (QFileInfo::exists(installed)) return installed;
  return QString();
}

bool OChartsService::daemonAvailable() const {
  const QString p = daemonPath();
  return !p.isEmpty() && QFileInfo(p).isExecutable();
}

void OChartsService::setStatus(const QString& text, bool busy) {
  m_status = text;
  m_busy = busy;
  Q_EMIT changed();
}

void OChartsService::probeVersion() {
  const QString daemon = daemonPath();
  if (daemon.isEmpty()) return;
  // `-a` prints "oexserverd Version X.YZ" and exits (does not daemonize).
  auto* proc = new QProcess(this);
  connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, proc](int, QProcess::ExitStatus) {
            const QString out = QString::fromUtf8(proc->readAllStandardOutput());
            const QString line = out.trimmed();
            if (!line.isEmpty()) {
              m_version = line.section('\n', 0, 0).trimmed();
              Q_EMIT changed();
            }
            proc->deleteLater();
          });
  proc->start(daemon, {QStringLiteral("-a")});
}

void OChartsService::generateFingerprint() {
  if (m_busy) return;
  const QString daemon = daemonPath();
  if (daemon.isEmpty()) {
    setStatus(tr("oexserverd not found — install o-charts or vendor the daemon"),
              false);
    return;
  }
  m_fpr_file.clear();

  // Output directory for the .fpr (the daemon names the file itself, and
  // concatenates dir+filename WITHOUT a separator, so pass a trailing slash).
  const QString out_dir =
      QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
      QStringLiteral("/ocpn-fpr/");
  QDir().mkpath(out_dir);

  setStatus(tr("Generating fingerprint…"), true);

  auto* proc = new QProcess(this);
  // oexserverd dlopen()s its SGLock crypto dylib (libsglmac…) from the working
  // directory, so run it from the daemon's own folder.
  proc->setWorkingDirectory(QFileInfo(daemon).absolutePath());
  proc->setProcessChannelMode(QProcess::MergedChannels);
  connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, proc, out_dir](int, QProcess::ExitStatus) {
            const QString out = QString::fromUtf8(proc->readAll());
            proc->deleteLater();
            // The daemon prints a line containing "FPR:<path>".
            QString fpr;
            for (const QString& line : out.split('\n')) {
              const int idx = line.indexOf(QStringLiteral("FPR"),
                                           0, Qt::CaseInsensitive);
              if (idx >= 0 && line.contains(':')) {
                fpr = line.section(':', 1).trimmed();
                break;
              }
            }
            // Fall back to any .fpr the daemon left in the output dir.
            if (fpr.isEmpty() || !QFileInfo::exists(fpr)) {
              const QStringList found =
                  QDir(out_dir).entryList({QStringLiteral("*.fpr")}, QDir::Files,
                                          QDir::Time);
              if (!found.isEmpty()) fpr = out_dir + "/" + found.first();
            }
            if (fpr.isEmpty() || !QFileInfo::exists(fpr)) {
              setStatus(tr("Fingerprint generation failed (no .fpr produced)"),
                        false);
              return;
            }
            // Copy to the Desktop so the user can upload it to o-charts.
            const QString desktop = QStandardPaths::writableLocation(
                QStandardPaths::DesktopLocation);
            QString dest = fpr;
            if (!desktop.isEmpty()) {
              dest = desktop + "/" + QFileInfo(fpr).fileName();
              QFile::remove(dest);
              if (QFile::copy(fpr, dest)) {
                m_fpr_file = dest;
              } else {
                m_fpr_file = fpr;
              }
            } else {
              m_fpr_file = fpr;
            }
            setStatus(tr("Fingerprint saved: %1").arg(m_fpr_file), false);
          });
  connect(proc, &QProcess::errorOccurred, this,
          [this, proc](QProcess::ProcessError) {
            setStatus(tr("Could not run oexserverd"), false);
            proc->deleteLater();
          });
  // `oexserverd -g "<dir>"` -- the normal (non-dongle) machine fingerprint.
  proc->start(daemon, {QStringLiteral("-g"), out_dir});
}

}  // namespace ocpn::qtui
