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
import QtQuick.Layouts
import opencpn.qt

// MUIBar -- per-canvas view controls, bottom-right (mirrors OpenCPN's MUIBar).
// Zoom in/out, the Follow / jump-to-ship split button (with a flyout of the
// centred / look-ahead variants), and a menu opening the canvas display options.
//
// Single-backdrop flyout: rather than a separate floating panel, the flyout is
// an animated BULGE of the bar's OWN backdrop -- the toolbar pill and the bulge
// are drawn as ONE shape (one translucent fill, one border, concave fillets
// where the bulge meets the bar), so it literally grows out of the toolbar. The
// flyout's buttons sit in that bulge. (A separate translucent panel can never
// merge: the alphas compound and the bar's border shows through.)
//
// Styling matches FloatToolbar.qml (keep in sync): translucency lives in the
// background colours' alpha (NOT opacity, which would dim the icons); button
// chips a touch less transparent than the panel; icon glyphs fully opaque.
Item {
    id: muiBar
    implicitWidth: barRow.implicitWidth + 2 * pad
    implicitHeight: barRow.implicitHeight + 2 * pad

    // Per-tool touch size (bound from the window's touchSize -- same control-
    // sizing option as the master toolbar). Drives the bar AND the flyout chips.
    property real touchSize: 44
    readonly property real pad: 3
    readonly property real panelAlpha: 1.0 - UIConfig.toolbarTransparency
    readonly property real buttonAlpha: panelAlpha + (1.0 - panelAlpha) * 0.4
    readonly property real borderAlpha: panelAlpha * 0.4

    signal canvasOptionsRequested()

    // --- Flyout (single-backdrop bulge) ---
    property bool followOpen: false
    property real prog: 0  // 0..1 bulge-grown fraction, animated
    onFollowOpenChanged: prog = followOpen ? 1 : 0
    Behavior on prog { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

    // Bulge size: the follow flyout is two stacked options above the follow btn.
    // Flyout chips use the SAME 2px gap as the bar's buttons.
    readonly property real flyGap: 2
    readonly property real bulgeContentW: touchSize
    readonly property real bulgeContentH: 2 * touchSize + flyGap
    readonly property real bulgeW: bulgeContentW + 2 * flyGap
    readonly property real bulgeH: bulgeContentH + 2 * flyGap
    readonly property real followCx: barRow.x + followBtn.x + followBtn.width / 2

    // Follow control as a 3-state ROTATION: off / centred / look-ahead. The bar
    // shows the ACTIVE state; the flyout shows the other two. Selecting one
    // rotates it onto the bar and returns the previous bar state to the flyout.
    readonly property var followGlyphs: ["⊙", "◉", "➤"]
    readonly property var followTips: [qsTr("Follow / jump to ship"),
                                       qsTr("Following (centred)"),
                                       qsTr("Following (look-ahead)")]
    readonly property int followActive: {
        var c = root.activeChart || chart
        if (!c || !c.followOwnShip) return 0
        return DisplayConfig.lookAhead ? 2 : 1
    }
    readonly property var followOthers:
        [0, 1, 2].filter(function(i) { return i !== muiBar.followActive })
    function applyFollow(i) {
        var c = root.activeChart || chart
        if (!c) return
        DisplayConfig.lookAhead = (i === 2)
        c.followOwnShip = (i !== 0)
    }

    // Shared tool factory (same chip styling as FloatToolbar's Tool): used by
    // the bar itself AND the flyout, so they're identical.
    component MuiTool: ToolButton {
        id: ctl
        font.pointSize: 16
        implicitWidth: muiBar.touchSize; implicitHeight: muiBar.touchSize
        ToolTip.visible: hovered && ToolTip.text.length > 0
        ToolTip.delay: 400
        background: Rectangle {
            radius: 4
            readonly property real a: muiBar.buttonAlpha
            color: (ctl.checked || ctl.down || ctl.highlighted)
                       ? Qt.rgba(0.30, 0.47, 0.75, Math.max(0.9, a))
                   : ctl.hovered ? Qt.rgba(0.78, 0.84, 0.93, a)
                   : Qt.rgba(0.87, 0.88, 0.91, a)
        }
        contentItem: Text {
            text: ctl.text
            font: ctl.font
            color: (ctl.checked || ctl.down || ctl.highlighted) ? "white"
                                                                 : "#1b1d21"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // The ONE backdrop: bar pill plus the animated bulge, a single path with a
    // single fill + border. Extends above the bar to host the bulge; never
    // intercepts input (the buttons sit on top).
    Canvas {
        id: backdrop
        enabled: false
        x: 0
        y: -muiBar.bulgeH
        width: muiBar.width
        height: muiBar.bulgeH + muiBar.height
        readonly property string key: [width, height, muiBar.prog, muiBar.panelAlpha,
                                       muiBar.borderAlpha, muiBar.followCx,
                                       muiBar.bulgeW].join(",")
        onKeyChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        function pill(ctx, x, y, w, h, r) {
            ctx.moveTo(x + r, y)
            ctx.lineTo(x + w - r, y); ctx.arcTo(x + w, y, x + w, y + r, r)
            ctx.lineTo(x + w, y + h - r); ctx.arcTo(x + w, y + h, x + w - r, y + h, r)
            ctx.lineTo(x + r, y + h); ctx.arcTo(x, y + h, x, y + h - r, r)
            ctx.lineTo(x, y + r); ctx.arcTo(x, y, x + r, y, r)
            ctx.closePath()
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var W = width, rB = 6, rT = 4
            var barTop = muiBar.bulgeH
            var barBot = height
            var prog = muiBar.prog
            ctx.beginPath()
            if (prog < 0.01) {
                pill(ctx, 0, barTop, W, barBot - barTop, rB)
            } else {
                var f = 8 * prog            // fillet depth grows with the bulge
                var rt = rT * prog
                var bx0 = muiBar.followCx - muiBar.bulgeW / 2
                var bx1 = muiBar.followCx + muiBar.bulgeW / 2
                var bTop = barTop - prog * muiBar.bulgeH
                ctx.moveTo(rB, barTop)
                ctx.lineTo(bx0 - f, barTop)
                ctx.arc(bx0 - f, barTop - f, f, Math.PI / 2, 0, true)        // fillet up
                ctx.lineTo(bx0, bTop + rt)
                ctx.arcTo(bx0, bTop, bx0 + rt, bTop, rt)                     // bulge TL
                ctx.lineTo(bx1 - rt, bTop)
                ctx.arcTo(bx1, bTop, bx1, bTop + rt, rt)                     // bulge TR
                ctx.lineTo(bx1, barTop - f)
                ctx.arc(bx1 + f, barTop - f, f, Math.PI, Math.PI / 2, true)  // fillet down
                ctx.lineTo(W - rB, barTop)
                ctx.arcTo(W, barTop, W, barTop + rB, rB)                     // bar TR
                ctx.lineTo(W, barBot - rB)
                ctx.arcTo(W, barBot, W - rB, barBot, rB)                     // bar BR
                ctx.lineTo(rB, barBot)
                ctx.arcTo(0, barBot, 0, barBot - rB, rB)                     // bar BL
                ctx.lineTo(0, barTop + rB)
                ctx.arcTo(0, barTop, rB, barTop, rB)                         // bar TL
                ctx.closePath()
            }
            ctx.fillStyle = Qt.rgba(0.93, 0.93, 0.95, muiBar.panelAlpha)
            ctx.fill()
            ctx.lineWidth = 1
            ctx.strokeStyle = Qt.rgba(0, 0, 0, muiBar.borderAlpha)
            ctx.stroke()
        }
    }

    // The bar's own tools.
    RowLayout {
        id: barRow
        z: 2  // above the click-away dismiss, so bar buttons stay live
        x: muiBar.pad; y: muiBar.pad
        spacing: 2
        MuiTool {
            text: "+"; font.pointSize: 19; ToolTip.text: qsTr("Zoom in")
            visible: UIConfig.showZoomButtons  // Options > User Interface
            onClicked: (root.activeChart || chart).zoomIn()
        }
        MuiTool {
            text: "−"; font.pointSize: 19; ToolTip.text: qsTr("Zoom out")
            visible: UIConfig.showZoomButtons
            onClicked: (root.activeChart || chart).zoomOut()
        }
        // Follow / jump-to-ship split button. Tap cycles off -> follow (centred)
        // -> follow (look-ahead) -> off. Press-and-hold (or right-click) opens
        // the flyout to jump straight to a variant.
        MuiTool {
            id: followBtn
            text: muiBar.followGlyphs[muiBar.followActive]
            ToolTip.text: muiBar.followTips[muiBar.followActive]
            highlighted: muiBar.followActive !== 0
            // Tap cycles to the next state (the rotation); hold/right-click opens
            // the flyout of the other two.
            onClicked: {
                if (muiBar.followOpen) { muiBar.followOpen = false; return }
                muiBar.applyFollow((muiBar.followActive + 1) % 3)
            }
            onPressAndHold: muiBar.followOpen = true
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: muiBar.followOpen = true
            }
        }
        MuiTool {
            text: "☰"; ToolTip.text: qsTr("Canvas display options")
            onClicked: muiBar.canvasOptionsRequested()
        }
    }

    // Click-away dismiss: covers the parent while the flyout is open. Sits
    // BELOW the bar buttons (z) so they stay live (the follow button then
    // closes the flyout via its own handler), and below the flyout buttons.
    Item {
        z: 1
        visible: muiBar.followOpen
        x: -muiBar.x; y: -muiBar.y
        width: muiBar.parent ? muiBar.parent.width : 0
        height: muiBar.parent ? muiBar.parent.height : 0
        TapHandler { onTapped: muiBar.followOpen = false }
    }

    // Flyout buttons, in the bulge (same coordinate space as the backdrop, so
    // they align exactly). They rise out of the bar and fade in with `prog`.
    ColumnLayout {
        id: flyoutCol
        z: 3
        visible: muiBar.prog > 0.01
        opacity: muiBar.prog
        spacing: muiBar.flyGap
        x: muiBar.followCx - width / 2
        y: muiBar.flyGap - muiBar.prog * muiBar.bulgeH
        // The OTHER two states (rotation); picking one rotates it onto the bar.
        Repeater {
            model: muiBar.followOthers
            MuiTool {
                required property var modelData
                text: muiBar.followGlyphs[modelData]
                ToolTip.text: muiBar.followTips[modelData]
                onClicked: {
                    muiBar.applyFollow(modelData);
                    muiBar.followOpen = false;
                }
            }
        }
    }

    // Drag the bar within its parent (the chart overlay), like the master bar.
    DragHandler {
        target: muiBar
        xAxis.minimum: 0
        xAxis.maximum: muiBar.parent ? muiBar.parent.width - muiBar.width : 0
        yAxis.minimum: 0
        yAxis.maximum: muiBar.parent ? muiBar.parent.height - muiBar.height : 0
    }
}
