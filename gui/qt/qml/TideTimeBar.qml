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

// --- Time bar (P3.14 F): the graph's x-axis. A thin solid strip pinned to
//     the window bottom (above the status bar). The chart shrinks above it,
//     and further when the drawer opens. A fixed read-marker over a
//     pannable, infinite axis (drag to pan; click to read; ▶ animates;
//     Now re-snaps). Visible only when tides are on (MUIBar ≋).
Item {
    id: tideBar
    // anchors.left/right/bottom are set by the shell (Main.qml): the bar
    // spans the window bottom and yields to the vessel HUD on the right.
    height: DisplayConfig.showTides ? 30 : 0
    visible: DisplayConfig.showTides
    clip: true
    Behavior on height { NumberAnimation { duration: 150 } }

    // Single source of truth for the time->x mapping, consumed by the bar
    // ticks AND the graph Canvas. The plot starts past a left gutter so the
    // graph's y-axis lines up with the window's left edge.
    readonly property real plotLeft: plot.x
    readonly property real plotWidth: plot.width
    readonly property double winStartMs: TimeController.windowStart.getTime()
    readonly property double winEndMs: TimeController.windowEnd.getTime()

    Rectangle {  // one continuous panel with the drawer -- no borders/seams
        anchors.fill: parent
        color: "#0b1118"
    }

    Label {  // play/pause in the bottom-left corner (where the axes meet)
        id: playBtn
        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 8
        text: TimeController.playing ? "⏸" : "▶"
        color: playMA.containsMouse ? "#ffffff" : "#d8e2ec"; font.pointSize: 13
        MouseArea {
            id: playMA; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: TimeController.togglePlay()
        }
    }

    Item {  // plot: x-axis ticks + fixed marker + now line + readout + pan
        id: plot
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.leftMargin: 44   // y-axis gutter (axis title + figures)
        anchors.rightMargin: 8

        Canvas {
            id: barCanvas
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d"); ctx.reset()
                var s = tideBar.winStartMs, e = tideBar.winEndMs
                if (e <= s) return
                var span = e - s
                function xOf(ms) { return width * (ms - s) / span }
                var d = new Date(s)
                d.setMinutes(0, 0, 0); d.setHours(d.getHours() + 1)
                var days = ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"]
                for (var t = d.getTime(); t < e; t += 3600000) {
                    var x = xOf(t)
                    var dt = new Date(t)   // local time -- DST-correct labels
                    var hr = dt.getHours()
                    var major = (hr % 3 === 0)
                    // ticks hang from the top edge (the graph baseline)
                    ctx.strokeStyle = major ? "#80ffffff" : "#38ffffff"; ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.moveTo(x, 0); ctx.lineTo(x, major ? 9 : 5); ctx.stroke()
                    if (hr === 0) {
                        // Midnight: a full-height divider + the new day's name,
                        // so a multi-day pan keeps its bearings.
                        ctx.strokeStyle = "#60ffffff"
                        ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
                        ctx.fillStyle = "#d8e4f0"; ctx.font = "bold 9px sans-serif"
                        ctx.fillText(days[dt.getDay()] + " " + dt.getDate(), x + 3, height - 3)
                    } else if (major) {
                        ctx.fillStyle = "#b0c0d0"; ctx.font = "9px sans-serif"
                        ctx.fillText((hr < 10 ? "0" : "") + hr, x - 6, height - 3)
                    }
                }
            }
            Connections {
                target: TimeController
                function onWindowChanged() { barCanvas.requestPaint() }
            }
        }

        Rectangle {  // now line
            visible: TimeController.nowFraction >= 0 && TimeController.nowFraction <= 1
            x: plot.width * TimeController.nowFraction - 1
            width: 2; height: plot.height; color: "#ff5b5b"
        }
        // Event marks (e.g. GRIB forecast steps): a green dot per mark
        // at its position on the panning axis.
        Repeater {
            model: TimeController.marks
            delegate: Rectangle {
                required property var modelData
                readonly property double frac: {
                    const a = TimeController.windowStart.getTime() / 1000
                    const b = TimeController.windowEnd.getTime() / 1000
                    return b > a ? (modelData - a) / (b - a) : -1
                }
                visible: frac >= 0 && frac <= 1
                x: plot.width * frac - 3
                y: 2
                width: 6; height: 6; radius: 3
                color: "#7dd87d"
                border.color: "#1e5e1e"
            }
        }

        Rectangle {  // fixed read-marker (the axis pans under it)
            x: plot.width * TimeController.markerFraction - 1
            width: 2; height: plot.height; color: "#ffd27f"
        }

        MouseArea {
            id: panArea
            anchors.fill: parent
            property real lastX: 0
            property real pressX: 0
            onPressed: (m) => { lastX = m.x; pressX = m.x }
            onReleased: (m) => {
                if (Math.abs(m.x - pressX) < 4)
                    TimeController.setDisplayFraction(m.x / plot.width)
            }
            onPositionChanged: (m) => {
                if (pressed) {
                    TimeController.panPixels(m.x - lastX, plot.width)
                    lastX = m.x
                }
            }
        }

        // Date/time readout pinned beside the gold marker; double-click =
        // snap to Now (frees the right side for a full-width plot).
        Rectangle {
            id: readout
            x: Math.min(plot.width - width - 2,
                        plot.width * TimeController.markerFraction + 6)
            y: 1; height: 15; width: readoutText.width + 8; radius: 2
            color: Qt.rgba(0, 0, 0, 0.55)
            Text {
                id: readoutText; anchors.centerIn: parent
                text: TimeController.dateLabel + " " + TimeController.timeLabel
                color: TimeController.live ? "#8fd0ff" : "#ffd27f"
                font.pointSize: 9; font.bold: true
            }
            MouseArea {
                anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                property real lastX: 0
                onPressed: (m) => lastX = m.x
                onDoubleClicked: TimeController.goLive()
                onPositionChanged: (m) => {
                    if (pressed) {
                        TimeController.panPixels(m.x - lastX, plot.width); lastX = m.x
                    }
                }
                ToolTip.text: qsTr("Double-click to snap to now")
                ToolTip.visible: containsMouse; ToolTip.delay: 600
            }
        }
    }
}
