/***************************************************************************
 *   Copyright (C) 2016 Sean D'Epagnier                                    *
 *   Copyright (C) 2016 by David S.Register                                *
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
 * Implement chartdata_input_stream.h -- XZ compressed charts support
 */

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/log.h>
#include <wx/wfstream.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>

#include "config.h"
#include "model/chartdata_input_stream.h"
#include "model/wx_qt_string.h"

#ifdef OCPN_USE_LZMA

wxCompressedFFileInputStream::wxCompressedFFileInputStream(
    const wxString &fileName) {
  init_lzma();
  m_file = new QFile(wxString_to_QString(fileName));
  m_file->open(QIODevice::ReadOnly);
}

wxCompressedFFileInputStream::~wxCompressedFFileInputStream() {
  delete m_file;
  lzma_end(&strm);
}

bool wxCompressedFFileInputStream::IsOk() const {
  return wxStreamBase::IsOk() && m_file && m_file->isOpen();
}

size_t wxCompressedFFileInputStream::OnSysRead(void *buffer, size_t size) {
  lzma_action action = LZMA_RUN;

  strm.next_out = (uint8_t *)buffer;
  strm.avail_out = size;

  for (;;) {
    if (strm.avail_in == 0) {
      if (!m_file->atEnd()) {
        strm.next_in = inbuf;
        qint64 nread =
            m_file->read(reinterpret_cast<char *>(inbuf), sizeof inbuf);
        if (nread < 0) {
          // QFile error -- treat as read error
          m_lasterror = wxSTREAM_READ_ERROR;
          return 0;
        }
        strm.avail_in = static_cast<size_t>(nread);
      } else
        action = LZMA_FINISH;
    }

    lzma_ret ret = lzma_code(&strm, action);

    if (strm.avail_out == 0 || ret == LZMA_STREAM_END)
      return size - strm.avail_out;

    if (ret != LZMA_OK) {
      m_lasterror = wxSTREAM_READ_ERROR;
      return 0;
    }
  }
  return 0;
}

wxFileOffset wxCompressedFFileInputStream::OnSysSeek(wxFileOffset pos,
                                                     wxSeekMode mode) {
  // rewind to start is possible
  if (pos == 0 && mode == wxFromStart) {
    lzma_end(&strm);
    init_lzma();
    return m_file->seek(0) ? 0 : wxInvalidOffset;
  }

  return wxInvalidOffset;
}

wxFileOffset wxCompressedFFileInputStream::OnSysTell() const {
  return strm.total_out;
}

void wxCompressedFFileInputStream::init_lzma() {
  lzma_stream s = LZMA_STREAM_INIT;
  memcpy(&strm, &s, sizeof s);
  lzma_ret ret = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);

  if (ret != LZMA_OK) m_lasterror = wxSTREAM_READ_ERROR;
}

ChartDataNonSeekableInputStream::ChartDataNonSeekableInputStream(
    const wxString &fileName) {
  if (fileName.Upper().EndsWith("XZ"))
    m_stream = new wxCompressedFFileInputStream(fileName);
  else
    m_stream = new wxFFileInputStream(fileName);
}

ChartDataNonSeekableInputStream::~ChartDataNonSeekableInputStream() {
  delete m_stream;
}

size_t ChartDataNonSeekableInputStream::OnSysRead(void *buffer, size_t size) {
  m_stream->Read(buffer, size);
  return m_stream->LastRead();
}

wxFileOffset ChartDataNonSeekableInputStream::OnSysSeek(wxFileOffset pos,
                                                        wxSeekMode mode) {
  return m_stream->SeekI(pos, mode);
}

wxFileOffset ChartDataNonSeekableInputStream::OnSysTell() const {
  return m_stream->TellI();
}

ChartDataInputStream::ChartDataInputStream(const wxString &fileName) {
  if (fileName.Upper().EndsWith("XZ")) {
    // decompress to temp file to allow seeking
    QFileInfo fi(wxString_to_QString(fileName));
    QString tmpl =
        QDir::tempPath() + QDir::separator() + fi.fileName() + "_XXXXXX";
    QTemporaryFile tmp_file(tmpl);
    tmp_file.setAutoRemove(false);
    if (tmp_file.open()) {
      m_tempfilename = QString_to_wxString(tmp_file.fileName());
      // We close the temp file so we can write through QFile with our own
      // handle (matching the prior wxFFileOutputStream semantics).
      tmp_file.close();
    }
    wxCompressedFFileInputStream stream(fileName);
    QFile tmp(wxString_to_QString(m_tempfilename));
    tmp.open(QIODevice::WriteOnly);

    char buffer[8192];
    int len;
    do {
      stream.Read(buffer, sizeof buffer);
      len = stream.LastRead();
      tmp.write(buffer, len);
    } while (len == sizeof buffer);

    // do some error checking here?

    tmp.close();
    m_stream = new wxFFileInputStream(m_tempfilename);
  } else
    m_stream = new wxFFileInputStream(fileName);
}

ChartDataInputStream::~ChartDataInputStream() {
  // close it
  delete m_stream;
  // delete the temp file, how do we remove temp files if the program crashed?
  if (!m_tempfilename.empty())
    QFile::remove(wxString_to_QString(m_tempfilename));
}

size_t ChartDataInputStream::OnSysRead(void *buffer, size_t size) {
  m_stream->Read(buffer, size);
  return m_stream->LastRead();
}

wxFileOffset ChartDataInputStream::OnSysSeek(wxFileOffset pos,
                                             wxSeekMode mode) {
  return m_stream->SeekI(pos, mode);
}

wxFileOffset ChartDataInputStream::OnSysTell() const {
  return m_stream->TellI();
}

bool DecompressXZFile(const wxString &input_path, const wxString &output_path) {
  if (!QFile::exists(wxString_to_QString(input_path))) {
    return false;
  }
  wxCompressedFFileInputStream in(input_path);
  QFile out(wxString_to_QString(output_path));
  out.open(QIODevice::WriteOnly);

  char buffer[8192];
  int len;
  do {
    in.Read(buffer, sizeof buffer);
    len = in.LastRead();
    out.write(buffer, len);
  } while (len == sizeof buffer);

  return in.GetLastError() != wxSTREAM_READ_ERROR;
}

#else  // OCPN_USE_LZMA

bool DecompressXZFile(const wxString &input_path, const wxString &output_path) {
  wxLogMessage("Failed to decompress: " + input_path);
  wxLogMessage("OpenCPN compiled without liblzma support");

  return false;
}

#endif  // OCPN_USE_LZMA
