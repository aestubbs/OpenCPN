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

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#include <QAtomicInt>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QXmlStreamReader>

#ifndef OCPN_QT_OEXSERVERD
#define OCPN_QT_OEXSERVERD ""
#endif

namespace ocpn::qtui {

namespace {
// oexserverd (current OESU) public request FIFO + protocol constants.
constexpr char kPublicPipe[] = "/tmp/OCPN_PIPEX";
constexpr unsigned char kCmdReadOesu = 8;     // decrypt .oesu body to pipe
constexpr unsigned char kCmdReadOesuHdr = 9;  // decrypt only the header

// The request message written to the public FIFO. All char arrays, so it is
// naturally packed (1025 bytes). Mirrors o-charts_pi src/Osenc.h fifo_msg.
struct OexFifoMsg {
  unsigned char cmd;
  char fifo_name[256];
  char senc_name[256];
  char senc_key[512];
};
}  // namespace

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

int OChartsService::loadKeyList(const QString& chartDir) {
  m_keys.clear();
  const QStringList xmls =
      QDir(chartDir).entryList({QStringLiteral("*.XML"), QStringLiteral("*.xml")},
                               QDir::Files);
  for (const QString& name : xmls) {
    // `-sgl` files are dongle key lists; system keys live in the rest.
    if (name.contains(QStringLiteral("-sgl"), Qt::CaseInsensitive)) continue;
    QFile f(chartDir + QStringLiteral("/") + name);
    if (!f.open(QIODevice::ReadOnly)) continue;
    QXmlStreamReader xml(&f);
    QString fileName, key;
    bool inChart = false;
    while (!xml.atEnd()) {
      const auto tok = xml.readNext();
      if (tok == QXmlStreamReader::StartElement) {
        const QStringView n = xml.name();
        if (n == QStringLiteral("Chart")) {
          inChart = true; fileName.clear(); key.clear();
        } else if (inChart && n == QStringLiteral("FileName")) {
          fileName = xml.readElementText().trimmed();
        } else if (inChart && n == QStringLiteral("RInstallKey")) {
          key = xml.readElementText().trimmed();
        }
      } else if (tok == QXmlStreamReader::EndElement &&
                 xml.name() == QStringLiteral("Chart")) {
        if (!fileName.isEmpty() && !key.isEmpty()) m_keys.insert(fileName, key);
        inChart = false;
      }
    }
  }
  return m_keys.size();
}

QString OChartsService::keyForCell(const QString& cellPath) const {
  return m_keys.value(QFileInfo(cellPath).completeBaseName());
}

bool OChartsService::ensureDaemon() {
  // A live server means the public FIFO can be opened for writing (a reader is
  // present). A stale pipe with no reader returns ENXIO -- treat as not-up and
  // (re)spawn, else the write-open would block forever.
  auto readerPresent = []() {
    const int fd = ::open(kPublicPipe, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
      ::close(fd);
      return true;
    }
    return false;
  };
  if (readerPresent()) return true;
  const QString daemon = daemonPath();
  if (daemon.isEmpty()) return false;
  // Bare invocation = FIFO server mode; run from its own dir so it finds
  // libsglmac via dlopen.
  QProcess::startDetached(daemon, {}, QFileInfo(daemon).absolutePath());
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < 5000) {
    QThread::msleep(100);
    if (readerPresent()) return true;
  }
  return readerPresent();
}

QByteArray OChartsService::decryptCell(const QString& cellPath, bool& ok) {
  return decrypt(cellPath, kCmdReadOesu, ok);
}

QByteArray OChartsService::decryptCellHeader(const QString& cellPath, bool& ok) {
  return decrypt(cellPath, kCmdReadOesuHdr, ok);
}

QByteArray OChartsService::decrypt(const QString& cellPath, unsigned char cmd,
                                   bool& ok) {
  ok = false;
  const QString key = keyForCell(cellPath);
  if (key.isEmpty()) return {};
  if (!ensureDaemon()) return {};

  // Unique private return FIFO for the daemon to stream the plaintext into.
  static QAtomicInt seq(0);
  const QString fifo = QStringLiteral("/tmp/ocpn_qt_%1_%2")
                           .arg(QCoreApplication::applicationPid())
                           .arg(seq.fetchAndAddRelaxed(1));
  const QByteArray fifo_c = fifo.toUtf8();
  ::unlink(fifo_c.constData());
  if (::mkfifo(fifo_c.constData(), 0666) != 0) return {};

  // Build + send the request to the public FIFO.
  OexFifoMsg msg;
  std::memset(&msg, 0, sizeof(msg));
  msg.cmd = cmd;
  std::strncpy(msg.fifo_name, fifo_c.constData(), sizeof(msg.fifo_name) - 1);
  const QByteArray cell_c = cellPath.toUtf8();
  std::strncpy(msg.senc_name, cell_c.constData(), sizeof(msg.senc_name) - 1);
  const QByteArray key_c = key.toUtf8();
  std::strncpy(msg.senc_key, key_c.constData(), sizeof(msg.senc_key) - 1);

  int wfd = ::open(kPublicPipe, O_WRONLY);
  if (wfd < 0) { ::unlink(fifo_c.constData()); return {}; }
  const ssize_t wn = ::write(wfd, &msg, sizeof(msg));
  ::close(wfd);
  if (wn != static_cast<ssize_t>(sizeof(msg))) {
    ::unlink(fifo_c.constData());
    return {};
  }

  // Read the decrypted stream from our private FIFO. The daemon writes the
  // SERVER_STATUS_RECORD and the OSENC body in SEPARATE FIFO open/close
  // sessions, so an EOF (read==0) is NOT necessarily the end -- it may just be
  // the gap before the body session. Keep the fd open and retry across EOF
  // (mirrors the wx "slow server" retry), stopping only after an idle period
  // with no further data. Non-blocking so we never hang on a dead daemon.
  // The daemon writes the SERVER_STATUS_RECORD and the OSENC body in SEPARATE
  // FIFO sessions (open/write/close). With O_NONBLOCK: read==0 means no writer
  // attached (EOF / between sessions); read<0 EAGAIN means a writer is present
  // but no data yet. Count writer sessions that actually delivered data and
  // stop after the second (status + body), so there is no fixed idle tail. A
  // grace after the first data-session and an overall cap keep us safe against
  // single-session daemons and a dead daemon.
  QByteArray out;
  int rfd = ::open(fifo_c.constData(), O_RDONLY | O_NONBLOCK);
  if (rfd >= 0) {
    char chunk[65536];
    int sessions = 0;          // data-bearing writer sessions that closed
    bool data_this_session = false;
    int idle_ms = 0, total_ms = 0;
    for (;;) {
      const ssize_t r = ::read(rfd, chunk, sizeof(chunk));
      if (r > 0) {
        out.append(chunk, static_cast<int>(r));
        data_this_session = true;
        idle_ms = 0;
      } else {
        if (r == 0 && data_this_session) {  // a writer session closed
          ++sessions;
          data_this_session = false;
          if (sessions >= 2) break;  // status + body done
        }
        QThread::msleep(10);
        idle_ms += 10;
        total_ms += 10;
        // After the body session, a brief grace ends a single-session daemon.
        if (sessions >= 1 && idle_ms > 300) break;
        if (total_ms > 8000) break;  // overall safety (no/slow daemon)
      }
    }
    ::close(rfd);
  }
  ::unlink(fifo_c.constData());
  ok = out.size() > 18;  // more than the SERVER_STATUS_RECORD alone
  return out;
}

}  // namespace ocpn::qtui
