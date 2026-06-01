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
#include <QStringList>

#include "chart_extent.h"  // CellExtent -- decode-free cell catalog entry
#include "s52_sg.h"        // s52sg::Buffer -- Qt-free world-coord geometry

namespace ocpn::qtui {

/** The S-52 decode-time display settings from the Vector Chart Display options
 *  (those that bake into the decoded geometry, so changing them requires a
 *  re-decode -- unlike the live per-provider filters: display category,
 *  soundings/text/lights/buoys). Mirrors the wx Options > Charts panel. */
struct ChartDisplaySettings {
  bool importantTextOnly = false;  // SetShowS57ImportantTextOnly
  bool useScamin = true;           // m_bUseSCAMIN (reduced detail at small scale)
  int symbolStyle = 0;             // 0 = paper chart, 1 = simplified (m_nSymbolStyle)
  int boundaryStyle = 0;           // 0 = plain, 1 = symbolised (m_nBoundaryStyle)
  int twoShades = 0;               // 0 = four-colour depth, 1 = two-colour
  double safetyContour = 5.0;      // metres (S52_MAR_SAFETY_CONTOUR/_DEPTH)
  double shallowContour = 2.0;     // metres (S52_MAR_SHALLOW_CONTOUR)
  double deepContour = 10.0;       // metres (S52_MAR_DEEP_CONTOUR)
  // Object-height display unit: m_nHeightUnitDisplay (0 = metres, 1 = feet).
  // s52plib bakes the converted value + suffix into VERCLR/HEIGHT/ELEVAT text
  // and light descriptions at decode time, so it rides the re-decode path.
  int heightUnit = 0;
  // Depth display unit in DisplayConfig order (0 = metres, 1 = feet,
  // 2 = fathoms). Soundings on SOUNDG features are formatted by the provider at
  // render time, but soundings baked onto wrecks/rocks/obstructions by s52plib's
  // SNDFRM02 (CS path) use m_nDepthUnitDisplay -- set at decode for consistency.
  int depthUnit = 0;
  // P2.16 -- the cartography toggles previously persisted-but-inert. All bake
  // into the decode (object/text selection + SCAMIN), so they ride the same
  // re-decode path as the fields above.
  bool chartInfoObjects = false;     // m_bShowMeta (M_* meta-object display)
  bool buoyLightLabels = true;       // SetShowAtonText (AtoN names)
  bool lightDescriptions = false;    // SetShowLdisText (light character text)
  bool extendedLightSectors = true;  // SetExtendLightSectors (full sector legs)
  bool nationalText = false;         // SetShowNationalText (NOBJNM etc.)
  bool declutterText = false;        // SetTextOverlapAvoid (m_bDeClutterText)
  bool superScamin = false;          // m_bUseSUPER_SCAMIN (extra-aggressive cull)
};

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

  /** Switch the S-52 colour scheme: 0=day, 1=dusk, 2=night. Mutates the
   *  shared s52plib colour table, so call it on the decode (worker) thread;
   *  already-decoded cells must be re-decoded to pick up the new palette. */
  void setColorScheme(int scheme);

  /** Apply the S-52 decode-time display settings to the shared s52plib (symbol/
   *  boundary style, depth shading + contours, important-text-only, SCAMIN).
   *  Like setColorScheme it mutates global s52plib state, so call it on the
   *  decode thread and re-decode resident cells afterwards. */
  void applyDisplaySettings(const ChartDisplaySettings& settings);

  /** Build a small synthetic S-57 chart covering [north,south]x[west,east]
   *  and decode it through s52plib into world-coordinate geometry (P2.8c).
   *  Returns an empty buffer if the engine is not initialised. This is the
   *  proof-of-pipeline for the scene-graph vector path -- real chart-cell
   *  loading replaces the synthetic feature construction later. */
  s52sg::Buffer buildDemoChart(double north, double south, double east,
                               double west);

  /** Load a real S-57 ENC cell (`path_000` -> a .000 file) via the OGR
   *  S-57 driver and decode it through s52plib into world-coordinate
   *  geometry (P2.8d). `s57data_dir` supplies the S-57 object-class /
   *  attribute CSVs the OGR driver needs. Geographic extent of the loaded
   *  cell is written to *out_north/south/east/west when non-null. Returns
   *  an empty buffer on failure. Currently emits area fills; lines and
   *  point features follow. */
  s52sg::Buffer loadEncCell(const QString& path_000,
                            const QString& s57data_dir,
                            double* out_north = nullptr,
                            double* out_south = nullptr,
                            double* out_east = nullptr,
                            double* out_west = nullptr);

  /** Load and merge several ENC cells into one geometry buffer (a larger
   *  experimental surface). Cells are decoded in order and accumulated;
   *  the combined geographic extent is written to the out params. */
  s52sg::Buffer loadEncCells(const QStringList& paths_000,
                             const QString& s57data_dir,
                             double* out_north = nullptr,
                             double* out_south = nullptr,
                             double* out_east = nullptr,
                             double* out_west = nullptr);

  /** Decode an OSENC ("SENC") record stream into world-coordinate geometry
   *  through s52plib, the same scene-graph buffer the OGR loader produces.
   *  `osenc` is the full plaintext OSENC bytes (a wx SENC cache *.S57, our
   *  SENC cache, or an o-charts cell decrypted by oexserverd). Cell extent is
   *  written to the out params. Emits point + line features now; area fills
   *  and soundings follow. Returns an empty buffer if not initialised. */
  //  `native_scale` is the cell's compilation scale (1:N); when > 0 and the
  //  super-SCAMIN mariner option is on, objects with no real SCAMIN get a
  //  synthesized one (P2.23b). 0 disables the synthesis.
  s52sg::Buffer decodeOsenc(const QByteArray& osenc, double* out_north = nullptr,
                            double* out_south = nullptr,
                            double* out_east = nullptr,
                            double* out_west = nullptr, int native_scale = 0);

  /** Convenience: read an OSENC file (*.S57 / decrypted *.oesenc/*.oeu) and
   *  decode it via decodeOsenc(). */
  s52sg::Buffer loadOsencCell(const QString& path, double* out_north = nullptr,
                              double* out_south = nullptr,
                              double* out_east = nullptr,
                              double* out_west = nullptr, int native_scale = 0);

  /** Decode-free catalog scan: open each cell via the OGR S-57 driver and
   *  union its feature envelopes into a CellExtent, WITHOUT running the
   *  s52plib symbology decode / tessellation. Much cheaper than a full load
   *  and (touching only OGR, never the global ps52plib) safe to run on the
   *  worker thread for a whole chart set. Cells that fail to open are
   *  skipped. Used to draw cell-coverage boundaries and seed on-demand
   *  loading. */
  QList<CellExtent> scanCellExtents(const QStringList& paths_000,
                                    const QString& s57data_dir);

  /** Scan a single cell's extent (see scanCellExtents). Returns an invalid
   *  CellExtent if the cell can't be opened. Lets the worker emit the
   *  catalog progressively so boundaries appear while a big set scans. */
  CellExtent scanOneCellExtent(const QString& path_000,
                               const QString& s57data_dir);

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
