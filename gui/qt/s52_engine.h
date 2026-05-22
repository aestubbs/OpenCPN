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
 * S52Engine -- Qt-friendly façade over the libs/s52plib library, the vendored
 * IHO S-52 vector-chart rendering engine.
 *
 * The s52plib library is wx-coupled (its public API takes wxString /
 * wxBitmap / wxGLContext etc.). This wrapper hides the wx surface behind a
 * Qt-clean (QString / QImage / ...) API so the rest of `gui/qt/` stays
 * wx-free. The implementation file (s52_engine.cpp) is the one place in
 * gui/qt/ that includes wxWidgets headers; opencpn-qt's wxWidgets link
 * dependency exists solely to satisfy s52plib transitively, and will go
 * away when libs/s52plib is de-wx'd (a separate later project; not
 * Phase 2).
 *
 * P2.8a -- just initialise the library and report status. Chart rendering
 * comes in P2.8b (RenderObjectToDC -> wxBitmap -> QImage -> QSGTexture
 * bitmap fallback) and P2.8c (true scene-graph port via
 * RenderObjectToQSG).
 */

#ifndef OCPN_QT_S52_ENGINE_H_
#define OCPN_QT_S52_ENGINE_H_

#include <memory>

#include <QObject>
#include <QString>

#include "s52_sg.h"  // s52sg::Buffer -- Qt-free world-coord geometry

namespace ocpn::qtui {

class S52Engine : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool ok READ isOk NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)

public:
  explicit S52Engine(QObject* parent = nullptr);
  ~S52Engine() override;

  /** Initialise s52plib. `data_dir` is the path to OpenCPN's `s57data/`
   *  directory (the one containing `S52RAZDS.RLE`, `chartsymbols.xml`,
   *  and `rastersymbols-{day,dusk,dark}.png`). Idempotent: a second
   *  call is a no-op. Returns true on success. */
  bool init(const QString& data_dir);

  /** True if s52plib was constructed successfully. */
  bool isOk() const;

  /** Human-readable status -- "S-52: initialised, presentation library
   *  v3.4 loaded from ..." or "S-52: init failed (could not load
   *  S52RAZDS.RLE from ...)". */
  QString status() const;

  /** Build a small synthetic S-57 chart covering [north,south]x[west,east]
   *  and decode it through s52plib into world-coordinate geometry (P2.8c).
   *  Returns an empty buffer if the engine is not initialised. This is the
   *  proof-of-pipeline for the scene-graph vector path -- real chart-cell
   *  loading replaces the synthetic feature construction later. */
  s52sg::Buffer buildDemoChart(double north, double south, double east,
                               double west);

Q_SIGNALS:
  void changed();

private:
  // PImpl -- hides the wx-typed s52plib pointer and the bookkeeping below
  // it. Header stays Qt-only.
  class Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_S52_ENGINE_H_
