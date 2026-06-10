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

// --- Tide/current graph drawer (P3.14 F): the graph grows UP out of the
//     top of the time bar, so the bar's hour ticks ARE the graph's x-axis
//     (no gap). Opens when a tide/current station is clicked; the y-axis
//     sits at the window's left edge. Drag the graph (or the bar) to pan.
Item {
    id: tideDrawer
    // anchors are set by the shell (Main.qml): the graph sits on the time
    // bar's top edge and yields to the vessel HUD on the right.

    // The TideTimeBar instance -- the single source of truth for the
    // time->x mapping (plotLeft/plotWidth/winStartMs/winEndMs).
    required property var timeBar

    readonly property bool open: DisplayConfig.showTides
        && chart.tideGraph && chart.tideGraph.valid
    height: open ? 170 : 0
    clip: true
    Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

    Rectangle {  // one continuous panel with the bar -- no borders/seams
        anchors.fill: parent
        color: "#0b1118"
    }

    Canvas {  // the tide/current curve, sharing the bar's plot x-range
        id: graphCanvas
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d"); ctx.reset()
            var tg = chart.tideGraph
            if (!tg || !tg.valid) return
            var pl = timeBar.plotLeft, pw = timeBar.plotWidth
            if (pw <= 1) return
            var topM = 22, botM = 2   // header band on top; curve meets the bar at the bottom
            var plotH = height - topM - botM
            if (plotH < 10) return
            var vmin = tg.minValue, vmax = tg.maxValue
            if (vmax <= vmin) vmax = vmin + 1
            function yOf(v) { return topM + plotH * (1 - (v - vmin) / (vmax - vmin)) }
            var n = Math.max(2, Math.round(pw / 2))
            var vals = tg.samples(timeBar.winStartMs, timeBar.winEndMs, n)
            if (vals.length < 2) return
            if (tg.isCurrent) {   // zero baseline for currents
                ctx.strokeStyle = "#40ffffff"; ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(pl, yOf(0)); ctx.lineTo(pl + pw, yOf(0)); ctx.stroke()
            }
            // filled curve
            ctx.beginPath(); ctx.moveTo(pl, yOf(vals[0]))
            for (var i = 1; i < vals.length; i++)
                ctx.lineTo(pl + pw * i / (vals.length - 1), yOf(vals[i]))
            var baseY = tg.isCurrent ? yOf(0) : (topM + plotH)
            ctx.lineTo(pl + pw, baseY); ctx.lineTo(pl, baseY); ctx.closePath()
            ctx.fillStyle = "#22384f"; ctx.fill()
            ctx.beginPath(); ctx.moveTo(pl, yOf(vals[0]))
            for (var j = 1; j < vals.length; j++)
                ctx.lineTo(pl + pw * j / (vals.length - 1), yOf(vals[j]))
            ctx.strokeStyle = "#5bb0ff"; ctx.lineWidth = 2; ctx.stroke()
            // current set vectors along the curve, every 15 min: a line in
            // the compass set (N up), length proportional to drift speed.
            if (tg.isCurrent) {
                var arr = tg.currentArrows(timeBar.winStartMs, timeBar.winEndMs, 15)
                var span2 = timeBar.winEndMs - timeBar.winStartMs
                ctx.strokeStyle = "#ffb347"; ctx.fillStyle = "#ffb347"; ctx.lineWidth = 1
                for (var a = 0; a < arr.length; a++) {
                    // length = drift over the configured time (same setting
                    // as the chart arrows), at a fixed graph scale.
                    var L = Math.min(34, arr[a].spd
                                     * (DisplayConfig.currentVectorMinutes / 60) * 130)
                    if (L < 1.5) continue   // skip near-slack (too short to read)
                    var ax = pl + pw * (arr[a].t - timeBar.winStartMs) / span2
                    var ay = yOf(arr[a].v)
                    var rad = arr[a].dir * Math.PI / 180
                    var ux = Math.sin(rad), uy = -Math.cos(rad)   // compass -> screen (N up)
                    var tx = ax + ux * L, ty = ay + uy * L
                    ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(tx, ty); ctx.stroke()
                    var nx = -uy, ny = ux                          // arrowhead
                    ctx.beginPath(); ctx.moveTo(tx, ty)
                    ctx.lineTo(tx - ux * 4 + nx * 2.2, ty - uy * 4 + ny * 2.2)
                    ctx.lineTo(tx - ux * 4 - nx * 2.2, ty - uy * 4 - ny * 2.2)
                    ctx.closePath(); ctx.fill()
                }
            }
            // now line -- full height so it continues down into the bar
            var nf = TimeController.nowFraction
            if (nf >= 0 && nf <= 1) {
                var nx = pl + pw * nf
                ctx.strokeStyle = "#ff5b5b"; ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(nx, topM); ctx.lineTo(nx, height); ctx.stroke()
            }
            // fixed read-marker; the value blob sits ON the drawn curve at
            // the marker (interpolate the samples) so it always intersects
            // the curve and the height is read off at the selected time.
            var mx = pl + pw * TimeController.markerFraction
            ctx.strokeStyle = "#ffd27f"; ctx.lineWidth = 2
            ctx.beginPath(); ctx.moveTo(mx, topM); ctx.lineTo(mx, height); ctx.stroke()
            var mIdx = TimeController.markerFraction * (vals.length - 1)
            var i0 = Math.max(0, Math.min(vals.length - 2, Math.floor(mIdx)))
            var mv = vals[i0] + (vals[i0 + 1] - vals[i0]) * (mIdx - i0)
            var my = yOf(mv)
            ctx.fillStyle = "#ffd27f"; ctx.beginPath(); ctx.arc(mx, my, 4.5, 0, 2 * Math.PI); ctx.fill()
            // height/speed read off the curve at the marker -- recomputed
            // every frame from the same samples, so it tracks the blob.
            var mtxt = (tg.isCurrent ? Math.abs(mv) : mv).toFixed(1) + " " + tg.unitLabel
            ctx.fillStyle = "#ffffff"; ctx.font = "bold 12px sans-serif"
            ctx.fillText(mtxt, Math.min(width - 70, mx + 8), Math.max(topM + 12, my - 8))
            // turning-point events (HW/LW or flood/ebb)
            var evs = tg.events(timeBar.winStartMs, timeBar.winEndMs)
            ctx.font = "10px sans-serif"
            for (var k = 0; k < evs.length; k++) {
                var ex = pl + pw * (evs[k].t - timeBar.winStartMs) / (timeBar.winEndMs - timeBar.winStartMs)
                var ey = yOf(evs[k].v)
                ctx.fillStyle = "#bcd8f5"
                ctx.beginPath(); ctx.arc(ex, ey, 2.5, 0, 2 * Math.PI); ctx.fill()
                var lbl = evs[k].type + " " + Number(evs[k].v).toFixed(1)
                var tw = ctx.measureText(lbl).width
                ctx.fillText(lbl, Math.max(pl, Math.min(pl + pw - tw, ex - tw / 2)), ey - 6)
            }
            // y-axis: a vertical line at t=0 (the left edge of the plot)
            // rising from the x-axis, with right-aligned figures + ticks.
            ctx.strokeStyle = "#6b7a8d"; ctx.lineWidth = 1
            ctx.beginPath(); ctx.moveTo(pl, topM); ctx.lineTo(pl, height); ctx.stroke()
            ctx.fillStyle = "#9fb1c4"; ctx.font = "10px sans-serif"
            ctx.textAlign = "right"; ctx.textBaseline = "middle"
            for (var g = 0; g <= 4; g++) {
                var vv = vmin + (vmax - vmin) * g / 4
                var gy = yOf(vv)
                ctx.beginPath(); ctx.moveTo(pl - 3, gy); ctx.lineTo(pl, gy); ctx.stroke()
                ctx.fillText(Number(vv).toFixed(1), pl - 6, gy)
            }
            // rotated axis title (replaces the unit that overlapped the top
            // figure): "Height (m)" for tides, "Speed (kn)" for currents.
            ctx.save()
            ctx.translate(9, topM + plotH / 2)
            ctx.rotate(-Math.PI / 2)
            ctx.textAlign = "center"; ctx.textBaseline = "middle"
            ctx.fillStyle = "#c0d0e0"; ctx.font = "10px sans-serif"
            ctx.fillText((tg.isCurrent ? "Speed (" : "Height (") + tg.unitLabel + ")", 0, 0)
            ctx.restore()
            ctx.textAlign = "left"; ctx.textBaseline = "alphabetic"
        }
        Connections {
            target: chart.tideGraph
            function onChanged() { graphCanvas.requestPaint() }
            function onMarkerChanged() { graphCanvas.requestPaint() }
        }
        Connections {
            target: DisplayConfig
            function onChanged() { graphCanvas.requestPaint() }
        }
        Timer {  // smooth repaint while the drawer is open (engine-sampled)
            interval: 33; repeat: true; running: tideDrawer.open
            onTriggered: graphCanvas.requestPaint()
        }
    }

    // Drag anywhere on the graph to pan time as well (mirrors the bar).
    MouseArea {
        anchors.fill: parent
        property real lastX: 0
        property real pressX: 0
        onPressed: (m) => { lastX = m.x; pressX = m.x }
        onReleased: (m) => {
            if (Math.abs(m.x - pressX) < 4 && timeBar.plotWidth > 1)
                TimeController.setDisplayFraction((m.x - timeBar.plotLeft) / timeBar.plotWidth)
        }
        onPositionChanged: (m) => {
            if (pressed && timeBar.plotWidth > 1) {
                TimeController.panPixels(m.x - lastX, timeBar.plotWidth)
                lastX = m.x
            }
        }
    }

    // Header band (on top of the canvas): station name + close.
    Label {
        anchors.top: parent.top; anchors.left: parent.left
        anchors.topMargin: 4; anchors.leftMargin: 8
        width: parent.width - 36
        text: chart.tideGraph && chart.tideGraph.valid
              ? chart.tideGraph.stationName : ""
        color: "#e8eef6"; font.bold: true; font.pointSize: 10
        elide: Text.ElideRight
    }
    Label {  // plain close glyph (no button background), top-right
        anchors.top: parent.top; anchors.right: parent.right
        anchors.topMargin: 3; anchors.rightMargin: 8
        text: "✕"; font.pointSize: 12
        color: closeMA.containsMouse ? "#ffffff" : "#8a97a6"
        MouseArea {
            id: closeMA; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: if (chart.tideGraph) chart.tideGraph.clear()
        }
    }
}
