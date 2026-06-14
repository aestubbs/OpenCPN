/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

import QtQuick
import QtQuick.Controls
import opencpn.qt

// --- Reusable toolbar flyout ---
// A small panel that grows OUT OF a toolbar button, perpendicular to the bar,
// merging into it: it overlaps the bar's edge and its base flares into the bar
// with a concave fillet, so it reads as part of the toolbar rather than a
// floating panel. Shares the toolbar's translucency and shade.
//
// Usage: parent it to the activating button and set `direction` perpendicular
// to the bar -- a vertical bar (master toolbar) uses "right"/"left"; a
// horizontal bar (MUI) uses "up"/"down". Put the flyout's controls as the
// default content.
Popup {
    id: flyout

    // Requested grow direction; the effective one (`_dir`) may flip near an edge.
    property string direction: "up"   // "up" | "down" | "left" | "right"

    // Render IN the chart scene (not a separate OS window): required so the
    // translucent background composites with the chart AND so the base can
    // visually merge with the in-scene toolbar. (Qt 6.8+ may otherwise pick a
    // native window, which is opaque over the scene and can't merge.)
    popupType: Popup.Item

    modal: false
    closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape

    // Geometry knobs.
    readonly property real _r: 8          // outer (far-edge) corner radius
    readonly property real _f: 11         // concave fillet + overlap into the bar
    readonly property real _pad: 4        // content inset on the non-merged sides

    // Same translucency model as the toolbars: the alpha lives in the colours,
    // never in Popup.opacity (which would dim contents too).
    readonly property real panelAlpha: 1.0 - UIConfig.toolbarTransparency

    // Per-side padding: extra room on the cross-axis for the fillet flare, and
    // on the merge side for the overlap into the bar.
    readonly property bool _vert: _dir === "up" || _dir === "down"
    leftPadding:   _vert ? _pad + _f : (_dir === "right" ? _pad + _f : _pad)
    rightPadding:  _vert ? _pad + _f : (_dir === "left"  ? _pad + _f : _pad)
    topPadding:    _vert ? (_dir === "down" ? _pad + _f : _pad) : _pad + _f
    bottomPadding: _vert ? (_dir === "up"   ? _pad + _f : _pad) : _pad + _f

    // Button rectangle in overlay coords, for edge-aware flipping.
    function _btn() {
        if (!parent)
            return Qt.rect(0, 0, 0, 0)
        var p = parent.mapToItem(Overlay.overlay, 0, 0)
        return Qt.rect(p.x, p.y, parent.width, parent.height)
    }

    // Honour the requested direction (the MUI bar wants "up" even though it sits
    // low on screen). No auto-flip -- toolbars don't sit at the far edges.
    readonly property string _dir: direction

    // Centre on the button's cross-axis; overlap the bar by `_f` on the merge
    // side so the base sinks into the toolbar.
    x: _dir === "left"  ? -width + _f
     : _dir === "right" ?  parent.width - _f
     : (parent.width - width) / 2
    y: _dir === "up"    ? -height + _f
     : _dir === "down"  ?  parent.height - _f
     : (parent.height - height) / 2

    // Merged background: a rounded body that flares into the bar with concave
    // fillets on the merge edge (drawn with Canvas so the fillets can be
    // concave -- a plain Rectangle can't). Same shade + alpha as the toolbar.
    background: Canvas {
        id: bg
        readonly property string fill: Qt.rgba(0.93, 0.93, 0.95, flyout.panelAlpha)
        // Repaint when size, direction or alpha changes.
        property string repaintKey: "" + width + "," + height + "," + flyout._dir
                                    + "," + flyout.panelAlpha
        onRepaintKeyChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = fill
            var W = width, H = height, r = flyout._r, f = flyout._f
            var d = flyout._dir
            // Concave fillets (tangent to the body side AND the bar edge) are
            // drawn with arc(); convex outer corners with arcTo(). Each path is
            // a closed silhouette: a rounded body that flares to full width/
            // height on the merge edge, which overlaps into the bar.
            ctx.beginPath()
            if (d === "up") {                 // merge edge = bottom (y=H)
                ctx.moveTo(f, r)
                ctx.arcTo(f, 0, f + r, 0, r)                       // TL convex
                ctx.lineTo(W - f - r, 0)
                ctx.arcTo(W - f, 0, W - f, r, r)                   // TR convex
                ctx.lineTo(W - f, H - f)
                ctx.arc(W, H - f, f, Math.PI, Math.PI / 2, true)   // BR fillet -> (W,H)
                ctx.lineTo(0, H)
                ctx.arc(0, H - f, f, Math.PI / 2, 0, true)         // BL fillet -> (f,H-f)
            } else if (d === "down") {        // merge edge = top (y=0)
                ctx.moveTo(f, H - r)
                ctx.arcTo(f, H, f + r, H, r)                       // BL convex
                ctx.lineTo(W - f - r, H)
                ctx.arcTo(W - f, H, W - f, H - r, r)               // BR convex
                ctx.lineTo(W - f, f)
                ctx.arc(W, f, f, Math.PI, 1.5 * Math.PI, false)    // TR fillet -> (W,0)
                ctx.lineTo(0, 0)
                ctx.arc(0, f, f, 1.5 * Math.PI, 2 * Math.PI, false) // TL fillet -> (f,f)
            } else if (d === "right") {       // merge edge = left (x=0)
                ctx.moveTo(0, f)
                ctx.arc(f, f, f, Math.PI, 1.5 * Math.PI, false)    // TL fillet -> (f,0)
                ctx.lineTo(W - r, 0)
                ctx.arcTo(W, 0, W, r, r)                           // TR convex
                ctx.lineTo(W, H - r)
                ctx.arcTo(W, H, W - r, H, r)                       // BR convex
                ctx.lineTo(f, H)
                ctx.arc(f, H - f, f, Math.PI / 2, Math.PI, false)  // BL fillet -> (0,H-f)
            } else {                          // left: merge edge = right (x=W)
                ctx.moveTo(W, f)
                ctx.arc(W - f, f, f, 0, -Math.PI / 2, true)        // TR fillet -> (W-f,0)
                ctx.lineTo(r, 0)
                ctx.arcTo(0, 0, 0, r, r)                           // TL convex
                ctx.lineTo(0, H - r)
                ctx.arcTo(0, H, r, H, r)                           // BL convex
                ctx.lineTo(W - f, H)
                ctx.arc(W - f, H - f, f, Math.PI / 2, 0, true)     // BR fillet -> (W,H-f)
            }
            ctx.closePath()
            ctx.fill()
        }
    }

    // Grow from the merged edge.
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 90 }
        NumberAnimation { property: "scale"; from: 0.85; to: 1.0
                          duration: 110; easing.type: Easing.OutBack }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 70 }
    }
    transformOrigin: _dir === "up"    ? Item.Bottom
                   : _dir === "down"  ? Item.Top
                   : _dir === "left"  ? Item.Right
                                      : Item.Left
}
