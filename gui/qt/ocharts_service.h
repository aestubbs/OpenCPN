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
 * OChartsService -- native (built-in, no plugin) o-charts support. Drives the
 * redistributable o-charts decryption daemon `oexserverd` (vendored under
 * third_party/oexserverd; OCPN_QT_OEXSERVERD). This first slice generates the
 * machine FINGERPRINT (`oexserverd -g`), which the user uploads to o-charts to
 * licence a chart set to this computer. The fingerprint is hardware-derived by
 * the daemon, so it is stable across our rebuilds. Decryption (a FIFO client
 * feeding Osenc::ingest200) is added on top of this once a licensed set exists.
 *
 * A QML_SINGLETON referenced as `OCharts`.
 */

#ifndef OCPN_QT_OCHARTS_SERVICE_H_
#define OCPN_QT_OCHARTS_SERVICE_H_

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QString>

namespace ocpn::qtui {

class OChartsService : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(OCharts)
  QML_SINGLETON
  // True if the oexserverd daemon binary is present + executable.
  Q_PROPERTY(bool daemonAvailable READ daemonAvailable NOTIFY changed)
  Q_PROPERTY(QString daemonVersion READ daemonVersion NOTIFY changed)
  // A running status / result line for the UI.
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  // The generated .fpr file path (empty until generated this session).
  Q_PROPERTY(QString fingerprintFile READ fingerprintFile NOTIFY changed)

public:
  static OChartsService& instance();
  static OChartsService* create(QQmlEngine*, QJSEngine*) {
    OChartsService* p = &instance();
    QJSEngine::setObjectOwnership(p, QJSEngine::CppOwnership);
    return p;
  }

  bool daemonAvailable() const;
  QString daemonVersion() const { return m_version; }
  QString status() const { return m_status; }
  bool busy() const { return m_busy; }
  QString fingerprintFile() const { return m_fpr_file; }

  /** Resolve the oexserverd binary: the vendored copy, else the installed
   *  o-charts_pi plugin copy. Empty if not found. */
  QString daemonPath() const;

  /** Run `oexserverd -g` to produce the machine fingerprint .fpr, copy it to
   *  the Desktop, and report the path. Async; updates status/busy/finger-
   *  printFile + emits changed(). */
  Q_INVOKABLE void generateFingerprint();

  /** Parse every keyList *.XML in `chartDir` (FileName -> RInstallKey) and
   *  MERGE them into the in-memory key map (does not clear existing keys, so
   *  it is safe to call once per chart directory). Returns the total number of
   *  keys held after the merge. Skips `-sgl` dongle key files for now (system
   *  keys only). Call clearKeys() before re-scanning all directories. */
  int loadKeyList(const QString& chartDir);

  /** Drop all loaded keys. Call once before re-loading every chart directory's
   *  keyList, so stale keys from removed dirs don't linger. */
  void clearKeys() { m_keys.clear(); }

  /** The RInstallKey for a cell file (matched by base name without extension),
   *  or empty if not in the loaded keyList. */
  QString keyForCell(const QString& cellPath) const;

  /** Decrypt an o-charts cell (*.oesu / *.oesenc) to a plaintext OSENC byte
   *  stream via oexserverd over the FIFO. BLOCKING -- call off the GUI thread
   *  (the chart worker). `ok` is set to the result. Empty on failure. The key
   *  is looked up from the loaded keyList (call loadKeyList first). */
  QByteArray decryptCell(const QString& cellPath, bool& ok);

  /** Decrypt only the cell HEADER (CMD_READ_OESU_HDR) -- cheap, for catalogue
   *  extent scans (the header carries CELL_EXTENT + native scale). */
  QByteArray decryptCellHeader(const QString& cellPath, bool& ok);

  /** Kick off the oexserverd FIFO server if it isn't already up, WITHOUT
   *  waiting. Call on the GUI thread (QProcess spawning is reliable there);
   *  the chart worker's decrypt then finds the daemon ready. */
  void prespawnDaemon();

Q_SIGNALS:
  void changed();

private:
  OChartsService();
  void setStatus(const QString& text, bool busy);
  void probeVersion();

  // Ensure the daemon is running (its FIFO exists), spawning it if needed.
  bool ensureDaemon();
  // Shared decrypt over the FIFO with the given command (8 = full OESU body,
  // 9 = header only). BLOCKING.
  QByteArray decrypt(const QString& cellPath, unsigned char cmd, bool& ok);

  QString m_version;
  QString m_status;
  bool m_busy = false;
  QString m_fpr_file;
  QHash<QString, QString> m_keys;  // cell base name -> RInstallKey
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_OCHARTS_SERVICE_H_
