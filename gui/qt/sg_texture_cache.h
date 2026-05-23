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
 * TextureCacheNode -- a QSGNode that owns the QSGTextures used by its
 * subtree and de-duplicates them by source-image identity (P2.5 texture
 * pipeline).
 *
 * The new chart code uploads QImages to the GPU via
 * QQuickWindow::createTextureFromImage. Two problems with calling that
 * ad hoc at each site:
 *   1. **Leaks / double-frees.** A QSGTextureMaterial does NOT own its
 *      texture, so a texture handed to one leaks when the subtree is freed;
 *      conversely, having every node setOwnsTexture(true) makes sharing a
 *      texture across nodes a double-free.
 *   2. **Duplication.** The same S-52 symbol or AP pattern bitmap recurs on
 *      many features in a cell; uploading it once per feature wastes VRAM
 *      and upload bandwidth.
 *
 * This node solves both: it is the ROOT of a provider's subtree, owns every
 * texture it vends, and frees them in its destructor. Because the
 * LayerCompositor frees a replaced subtree on the scene-graph **render
 * thread**, that destructor (and thus QSGTexture deletion) runs on the
 * render thread -- the same guarantee setOwnsTexture relies on. Vending is
 * keyed on QImage::cacheKey(), so callers that pass an implicitly-shared
 * QImage (e.g. the per-name memoised symbol/pattern bitmaps from
 * s52plib_sg.cpp) get one GPU texture for all instances. Nodes that draw
 * with a vended texture must call setOwnsTexture(false) (the default for a
 * raw material) -- this node owns it.
 *
 * Lifetime note: a texture is valid for as long as this node (the subtree
 * root) lives. The S-52 provider rebuilds the whole subtree on zoom-settle /
 * category change, so a rebuild makes a fresh cache and the old one's
 * textures are released with the old root -- no cross-rebuild reuse, but
 * the common pan path reuses the existing subtree (and its textures)
 * untouched.
 */

#ifndef OCPN_QT_SG_TEXTURE_CACHE_H_
#define OCPN_QT_SG_TEXTURE_CACHE_H_

#include <QHash>
#include <QImage>
#include <QQuickWindow>
#include <QSGNode>
#include <QSGTexture>

namespace ocpn::qtui {

class TextureCacheNode : public QSGNode {
public:
  explicit TextureCacheNode(QQuickWindow* window) : m_window(window) {}
  ~TextureCacheNode() override { qDeleteAll(m_textures); }

  TextureCacheNode(const TextureCacheNode&) = delete;
  TextureCacheNode& operator=(const TextureCacheNode&) = delete;

  /**
   * Return a texture for `image`, creating it on first use and reusing it for
   * any later image that shares the same cacheKey(). The returned pointer is
   * BORROWED -- this node owns it; do not delete it or setOwnsTexture(true).
   * Returns nullptr for a null image or if there is no window.
   */
  QSGTexture* texture(const QImage& image,
                      QQuickWindow::CreateTextureOptions options =
                          QQuickWindow::TextureHasAlphaChannel) {
    if (image.isNull() || !m_window) return nullptr;
    const qint64 key = image.cacheKey();
    auto it = m_textures.constFind(key);
    if (it != m_textures.constEnd()) return it.value();
    QSGTexture* tex = m_window->createTextureFromImage(image, options);
    if (tex) m_textures.insert(key, tex);
    return tex;
  }

  /** Number of distinct GPU textures held (diagnostics / tests). */
  int textureCount() const { return static_cast<int>(m_textures.size()); }

private:
  QQuickWindow* m_window;
  QHash<qint64, QSGTexture*> m_textures;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_SG_TEXTURE_CACHE_H_
