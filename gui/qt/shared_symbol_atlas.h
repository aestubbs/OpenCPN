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
 * SharedSymbolTextures -- a process-wide, render-thread-owned cache of GPU
 * textures keyed by QImage::cacheKey(), shared across every chart cell and both
 * split-view panes. Its one client today is the S-52 raster symbol library
 * SHEET (s52sg::Buffer::symbolSheet): the whole rastersymbols-*.png for the
 * current colour scheme, uploaded ONCE and drawn by every symbol via a
 * per-symbol sourceRect (Symbol::atlasRect).
 *
 * Why a shared SHEET, not per-cell or per-symbol textures:
 *   * Per-cell atlas (the previous design): each cell re-packed the library
 *     symbols into its OWN 2048x2048 pages (QPainter copy) and uploaded them on
 *     the render thread inside renderChart -- a chunk of the chart-load
 *     stutter, repeated for the 2nd split-view pane.
 *   * Per-symbol textures (a tried-and-reverted design): one texture per symbol
 *     type batches TERRIBLY -- symbols are emitted in mixed order, so
 *     consecutive QSGImageNodes rarely share a texture and the batch breaks on
 *     nearly every node. A symbol-dense cell went from a few draw calls to
 *     hundreds/thousands PER FRAME -> far WORSE pan stutter.
 *   * The library sheet is ALREADY one pre-packed atlas of every symbol. Upload
 *     it once; every symbol references one sub-rect of one texture, so they all
 *     batch into ~one draw call -- better than even the old per-cell atlas
 *     (whose pages differed per cell). Zero per-cell copy, zero re-upload.
 *
 * This singleton is TextureCacheNode (toolkit/sg_texture_cache.h) promoted to
 * APP LIFETIME: it vends one QSGTexture per unique source image (the sheet,
 * keyed by cacheKey), created on first use and reused by every cell and the
 * other pane. Entries are IMMUTABLE and the cache only ever grows, so a new
 * entry never disturbs a texture an already-built cell node references (no
 * dangling). A colour-scheme change yields a new sheet image (new cacheKey, a
 * new entry); the old sheet texture lingers until sceneGraphInvalidated -- a
 * handful of sheet textures at most, a few MB each, traded for zero
 * dangling-pointer risk.
 *
 * THREADING / LIFETIME -- the only subtle part:
 *   * Every method runs ONLY on the scene-graph render thread (from
 *     renderChart). The app has a single QQuickWindow (both ChartCanvas panes
 *     are children of one ApplicationWindow in Main.qml), hence one render
 *     context / one render thread, so a single shared texture serves both
 *     panes.
 *   * Textures are created via window->createTextureFromImage (needs the
 *     window's SG render context, which only exists after expose -- so this is
 *     NOT initialised in main()).
 *   * They are freed wholesale on QQuickWindow::sceneGraphInvalidated, on the
 *     render thread (a DirectConnection with `window` as context object). That
 *     fires when the context is going away and the whole scene graph is being
 *     torn down, so freeing every texture is safe.
 *   * The singleton is an intentionally-leaked heap object -- never a static
 *     with a destructor, which would run at process exit on the main thread
 *     after the render context is gone and try to delete textures there.
 *   * Colour-scheme / display-style changes are not special-cased: they
 *     re-decode cells with a new sheet image (new cacheKey -> a new entry); the
 *     old sheet texture just lingers (a few MB per scheme) until the next
 *     sceneGraphInvalidated. Not freeing mid-session is what keeps this free of
 *     dangling-pointer hazards.
 */

#ifndef OCPN_QT_SHARED_SYMBOL_ATLAS_H_
#define OCPN_QT_SHARED_SYMBOL_ATLAS_H_

#include <QHash>
#include <QImage>
#include <QPointer>
#include <QQuickWindow>
#include <QSGTexture>

namespace ocpn::qtui {

class SharedSymbolTextures {
public:
  /** The process-wide instance (intentionally never destroyed; see file doc). */
  static SharedSymbolTextures& instance();

  /**
   * Return a borrowed, app-lifetime texture for `image`, creating it once on
   * first use and reusing it for any later image that shares the same
   * cacheKey(). The returned pointer is owned by this singleton -- do NOT
   * delete it or setOwnsTexture(true) on the node that draws with it. Returns
   * nullptr for a null image or null window.
   *
   * Render thread only.
   */
  QSGTexture* texture(const QImage& image, QQuickWindow* window);

  /** Distinct GPU textures held (diagnostics / tests). */
  int textureCount() const { return static_cast<int>(m_textures.size()); }

private:
  SharedSymbolTextures() = default;
  SharedSymbolTextures(const SharedSymbolTextures&) = delete;
  SharedSymbolTextures& operator=(const SharedSymbolTextures&) = delete;

  /** Delete every texture and clear. Render thread only (context teardown). */
  void reset();

  QHash<qint64, QSGTexture*> m_textures;
  // The window whose render context owns the textures. We only ever see one
  // (single-window app); kept to detect a context change and to avoid
  // re-wiring the sceneGraphInvalidated connection more than once.
  QPointer<QQuickWindow> m_window;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_SHARED_SYMBOL_ATLAS_H_
