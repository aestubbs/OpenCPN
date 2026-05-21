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
 * Implement s52_engine.h.
 *
 * This is the only file in gui/qt/ that includes wxWidgets headers --
 * encapsulates the s52plib library's wx surface behind a Qt-clean façade.
 */

#include "s52_engine.h"

#include <wx/init.h>
#include <wx/image.h>
#include <wx/string.h>

#include "model/wx_qt_string.h"

#include "s52plib.h"

namespace ocpn::qtui {

class S52Engine::Impl {
public:
  s52plib* lib = nullptr;
  QString status = QStringLiteral("S-52: not yet initialised");
  bool wx_initialised = false;

  ~Impl() {
    delete lib;
    if (wx_initialised) wxUninitialize();
  }
};

S52Engine::S52Engine(QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>()) {}

S52Engine::~S52Engine() = default;

bool S52Engine::init(const QString& data_dir) {
  if (m_impl->lib) return m_impl->lib->m_bOK;

  // s52plib expects bare wx services (wxString conversion, wxLog, wxImage
  // PNG handler for the rasterised symbol sheets). wxInitialize is the
  // minimal init; an explicit wxImage PNG handler covers the symbol PNGs
  // without needing a wxApp.
  if (!wxIsMainThread()) {
    // wx insists on main thread for its global init. opencpn-qt's main
    // calls us from main; this branch is a safety check.
    m_impl->status = QStringLiteral("S-52: init must be called from main");
    Q_EMIT changed();
    return false;
  }
  m_impl->wx_initialised = wxInitialize();
  if (!wxImage::FindHandler(wxBITMAP_TYPE_PNG)) {
    wxImage::AddHandler(new wxPNGHandler());
  }

  const QString rle_path = data_dir + QStringLiteral("/S52RAZDS.RLE");
  m_impl->lib = new s52plib(QString_to_wxString(rle_path));
  if (!m_impl->lib->m_bOK) {
    m_impl->status =
        QStringLiteral("S-52: init failed (could not load %1)").arg(rle_path);
    Q_EMIT changed();
    return false;
  }

  m_impl->status =
      QStringLiteral("S-52: initialised; presentation library v%1.%2 loaded "
                     "from %3")
          .arg(m_impl->lib->GetMajorVersion())
          .arg(m_impl->lib->GetMinorVersion())
          .arg(data_dir);
  Q_EMIT changed();
  return true;
}

bool S52Engine::isOk() const {
  return m_impl->lib && m_impl->lib->m_bOK;
}

QString S52Engine::status() const {
  return m_impl->status;
}

}  // namespace ocpn::qtui
