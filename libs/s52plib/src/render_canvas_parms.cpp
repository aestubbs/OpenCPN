/***************************************************************************
 *   Copyright (C) 2010 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

/**
 * \file
 *
 * Trivial ctor/dtor for `render_canvas_parms` -- the parameter object used
 * by s52plib's fast-polygon pattern renderer. The class is declared in
 * s52s57.h and most of its fields are touched directly by s52plib; only
 * the constructor (which zeroes `pix_buff`) and the empty destructor were
 * defined out-of-line.
 *
 * Previously lived in gui/src/s57chart.cpp -- one of the gui/src->s52plib
 * leak points that made libs/s52plib non-self-contained at link time.
 * Moved here as part of P2.8.0c; see docs/QT_MIGRATION_TASKS.md.
 */

#include "s52s57.h"

render_canvas_parms::render_canvas_parms(void) { pix_buff = NULL; }

render_canvas_parms::~render_canvas_parms(void) {}
