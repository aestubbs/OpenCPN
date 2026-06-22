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
 * Implement shared_symbol_atlas.h.
 */

#include "shared_symbol_atlas.h"

namespace ocpn::qtui {

SharedSymbolTextures& SharedSymbolTextures::instance() {
  // Intentionally leaked: a render-thread-owned GPU cache must never run its
  // destructor at process exit on the main thread (the render context is gone
  // by then). GPU resources are reclaimed via sceneGraphInvalidated / teardown.
  static SharedSymbolTextures* s = new SharedSymbolTextures();
  return *s;
}

QSGTexture* SharedSymbolTextures::texture(const QImage& image,
                                          QQuickWindow* window) {
  if (image.isNull() || !window) return nullptr;

  // First bind to a window (or a re-created one after a context loss): wire the
  // teardown hook and start from an empty cache. createTextureFromImage ties a
  // texture to this window's render context, so a different window means the
  // old textures are invalid.
  if (m_window != window) {
    reset();
    m_window = window;
    // Free every texture when the render context is invalidated. DirectConnection
    // so the slot runs on the emitting (render) thread; `window` is the context
    // object so the connection dies with the window. We capture `this` (the
    // never-destroyed singleton) safely.
    QObject::connect(
        window, &QQuickWindow::sceneGraphInvalidated, window,
        [this]() { reset(); }, Qt::DirectConnection);
  }

  const qint64 key = image.cacheKey();
  auto it = m_textures.constFind(key);
  if (it != m_textures.constEnd()) return it.value();

  QSGTexture* tex =
      window->createTextureFromImage(image, QQuickWindow::TextureHasAlphaChannel);
  if (tex) m_textures.insert(key, tex);
  return tex;
}

void SharedSymbolTextures::reset() {
  // Drop the GPU textures only. Deliberately keep m_window: sceneGraphInvalidated
  // releases the render context but NOT the window object, and the next render
  // re-initialises the context, so we recreate textures lazily against the SAME
  // window without re-wiring the sceneGraphInvalidated connection (which would
  // otherwise accumulate one extra connection per invalidation).
  qDeleteAll(m_textures);
  m_textures.clear();
}

}  // namespace ocpn::qtui
