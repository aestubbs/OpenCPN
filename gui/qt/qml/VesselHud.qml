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

// --- Vessel data HUD: expandable right-edge panel. A SIBLING of the
//     chart (the chart anchors its right edge to this), so opening it
//     shrinks the chart rather than covering it. Gauges stacked
//     vertically; future "pages" can swap the stack.
Item {
    id: hudPanel
    anchors.right: parent.right
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    width: app.hudExpanded ? 300 : 0
    clip: true
    Behavior on width { NumberAnimation { duration: 150 } }

    readonly property var nav: chart.navState
    // Heading if available, else COG, for the boat/COG indicator.
    readonly property real hdg: nav && nav.hdgValid ? nav.hdg
                               : (nav ? nav.cog : 0)

    Rectangle {
        visible: app.hudExpanded
        anchors.fill: parent
        color: "#ee0e1216"; border.color: "#3affffff"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8

            Label {
                text: hudPanel.nav ? hudPanel.nav.positionText : "---"
                color: "#b0d0ff"; font.pointSize: 12; font.family: "monospace"
                Layout.alignment: Qt.AlignHCenter
            }

            // --- Gauge 1: compass with SOG/COG (north-up). ---
            Canvas {
                id: cogGauge
                Layout.alignment: Qt.AlignHCenter
                width: 250; height: 250
                property real cog: hudPanel.nav ? hudPanel.nav.cog : 0
                property real sog: hudPanel.nav ? hudPanel.nav.sog : 0
                property bool valid: hudPanel.nav && hudPanel.nav.ownShipValid
                property real hdg: hudPanel.hdg
                property bool hdgValid: hudPanel.nav && hudPanel.nav.hdgValid
                onCogChanged: requestPaint()
                onSogChanged: requestPaint()
                onValidChanged: requestPaint()
                onHdgChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d"); ctx.reset()
                    var cx = width/2, cy = height/2, r = width/2 - 6
                    var d2r = Math.PI/180
                    ctx.strokeStyle = "#c8d0d8"; ctx.lineWidth = 1.5
                    ctx.beginPath(); ctx.arc(cx, cy, r, 0, 2*Math.PI); ctx.stroke()
                    // Ticks + labels every 30 deg (north-up).
                    ctx.textAlign = "center"; ctx.textBaseline = "middle"
                    for (var d = 0; d < 360; d += 10) {
                        var a = (d - 90) * d2r
                        var major = (d % 30 === 0)
                        var inner = r - (major ? 14 : 7)
                        ctx.strokeStyle = (d > 0 && d < 180) ? "#4faf3a" : "#c83a3a"
                        ctx.lineWidth = major ? 2.5 : 1
                        ctx.beginPath()
                        ctx.moveTo(cx + inner*Math.cos(a), cy + inner*Math.sin(a))
                        ctx.lineTo(cx + r*Math.cos(a), cy + r*Math.sin(a))
                        ctx.stroke()
                        if (major) {
                            var lbl = (d===0?"N":d===90?"E":d===180?"S":d===270?"W":""+d)
                            var card = (d%90===0)
                            ctx.fillStyle = card ? "#c83a3a" : "#404850"
                            ctx.font = (card?"bold ":"") + "13px sans-serif"
                            var lr = r - 28
                            ctx.fillText(lbl, cx + lr*Math.cos(a), cy + lr*Math.sin(a))
                        }
                    }
                    // COG needle (orange wedge from centre to rim).
                    if (cogGauge.valid) {
                        var ca = (cogGauge.cog - 90) * d2r
                        ctx.fillStyle = "#e8651e"
                        ctx.beginPath()
                        ctx.moveTo(cx + r*Math.cos(ca), cy + r*Math.sin(ca))
                        ctx.lineTo(cx + 14*Math.cos(ca+1.55), cy + 14*Math.sin(ca+1.55))
                        ctx.lineTo(cx + 14*Math.cos(ca-1.55), cy + 14*Math.sin(ca-1.55))
                        ctx.closePath(); ctx.fill()
                    }
                    // HDG needle (white) drawn over the COG wedge.
                    if (cogGauge.hdgValid) {
                        var ha = (cogGauge.hdg - 90) * d2r
                        ctx.strokeStyle = "#ffffff"; ctx.lineWidth = 3
                        ctx.beginPath(); ctx.moveTo(cx, cy)
                        ctx.lineTo(cx + r*Math.cos(ha), cy + r*Math.sin(ha))
                        ctx.stroke()
                    }
                    // Centre hub with SOG + COG readout.
                    ctx.fillStyle = "#3a4048"
                    ctx.beginPath(); ctx.arc(cx, cy, r*0.55, 0, 2*Math.PI); ctx.fill()
                    ctx.fillStyle = "#aab4be"; ctx.font = "12px sans-serif"
                    ctx.fillText("SOG", cx, cy - r*0.30)
                    ctx.fillStyle = "#8fd14f"; ctx.font = "bold 26px sans-serif"
                    ctx.fillText(cogGauge.valid ? cogGauge.sog.toFixed(1)+" kt" : "-- kt",
                                 cx, cy - r*0.07)
                    ctx.fillStyle = "#aab4be"; ctx.font = "12px sans-serif"
                    ctx.fillText("COG", cx, cy + r*0.18)
                    ctx.fillStyle = "#ffffff"; ctx.font = "bold 22px sans-serif"
                    ctx.fillText(cogGauge.valid
                        ? (("00"+Math.round(cogGauge.cog)).slice(-3))+"°T" : "---°T",
                        cx, cy + r*0.36)
                }
            }
            // STW | HDG numerics under the compass.
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    Label {
                        text: hudPanel.nav && hudPanel.nav.stw >= 0
                              ? hudPanel.nav.stw.toFixed(2) : "--"
                        color:"#e8e8e8"; font.pointSize: 22; Layout.alignment: Qt.AlignHCenter
                    }
                    Label { text: "STW  kt"; color:"#808890"; font.pointSize: 10; Layout.alignment: Qt.AlignHCenter }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label {
                        text: hudPanel.nav && hudPanel.nav.hdgValid
                              ? hudPanel.nav.hdg.toFixed(0) : "--"
                        color:"#e8e8e8"; font.pointSize: 22; Layout.alignment: Qt.AlignHCenter
                    }
                    Label { text: "HDG  °T"; color:"#808890"; font.pointSize: 10; Layout.alignment: Qt.AlignHCenter }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#30ffffff" }

            // --- Gauge 2: apparent wind rose (bow-up). No wind feed
            //     yet (#39) -- drawn with the needle hidden + "--". ---
            Canvas {
                id: windGauge
                Layout.alignment: Qt.AlignHCenter
                width: 250; height: 250
                property real awa: hudPanel.nav && hudPanel.nav.awaValid ? hudPanel.nav.awa : -1
                property real aws: hudPanel.nav ? hudPanel.nav.aws : -1
                property real twa: hudPanel.nav && hudPanel.nav.twaValid ? hudPanel.nav.twa : -1
                property real tws: hudPanel.nav ? hudPanel.nav.tws : -1
                onAwaChanged: requestPaint()
                onAwsChanged: requestPaint()
                onTwaChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d"); ctx.reset()
                    var cx = width/2, cy = height/2, r = width/2 - 6
                    var d2r = Math.PI/180
                    ctx.strokeStyle = "#c8d0d8"; ctx.lineWidth = 1.5
                    ctx.beginPath(); ctx.arc(cx, cy, r, 0, 2*Math.PI); ctx.stroke()
                    ctx.textAlign = "center"; ctx.textBaseline = "middle"
                    // Bow-up: 0 at top, 180 at bottom; both sides.
                    for (var d = 0; d <= 360; d += 10) {
                        var a = (d - 90) * d2r
                        var major = (d % 30 === 0)
                        var inner = r - (major ? 14 : 7)
                        // Port (left, 180..360) red; stbd (right, 0..180) green.
                        ctx.strokeStyle = (d > 0 && d < 180) ? "#4faf3a" : "#c83a3a"
                        ctx.lineWidth = major ? 2.5 : 1
                        ctx.beginPath()
                        ctx.moveTo(cx + inner*Math.cos(a), cy + inner*Math.sin(a))
                        ctx.lineTo(cx + r*Math.cos(a), cy + r*Math.sin(a))
                        ctx.stroke()
                        if (major) {
                            var val = d <= 180 ? d : 360 - d  // mirror to 0..180
                            ctx.fillStyle = "#404850"; ctx.font = "13px sans-serif"
                            var lr = r - 26
                            ctx.fillText(""+val, cx + lr*Math.cos(a), cy + lr*Math.sin(a))
                        }
                    }
                    // Boat outline at centre (bow up).
                    ctx.strokeStyle = "#404850"; ctx.lineWidth = 2
                    ctx.beginPath()
                    ctx.moveTo(cx, cy - r*0.42)
                    ctx.quadraticCurveTo(cx + r*0.20, cy - r*0.10, cx + r*0.16, cy + r*0.30)
                    ctx.lineTo(cx - r*0.16, cy + r*0.30)
                    ctx.quadraticCurveTo(cx - r*0.20, cy - r*0.10, cx, cy - r*0.42)
                    ctx.stroke()
                    // TWA needle (blue) drawn first, then AWA on top.
                    if (windGauge.twa >= 0) {
                        var ta = (windGauge.twa - 90) * d2r
                        ctx.strokeStyle = "#3a8fd1"; ctx.lineWidth = 4
                        ctx.beginPath(); ctx.moveTo(cx, cy)
                        ctx.lineTo(cx + r*0.80*Math.cos(ta), cy + r*0.80*Math.sin(ta))
                        ctx.stroke()
                    }
                    // AWA needle (green) on top.
                    if (windGauge.awa >= 0) {
                        var wa = (windGauge.awa - 90) * d2r
                        ctx.strokeStyle = "#8fd14f"; ctx.lineWidth = 4
                        ctx.beginPath(); ctx.moveTo(cx, cy)
                        ctx.lineTo(cx + r*0.80*Math.cos(wa), cy + r*0.80*Math.sin(wa))
                        ctx.stroke()
                    }
                    ctx.fillStyle = "#8fd14f"; ctx.font = "bold 22px sans-serif"
                    ctx.fillText(windGauge.aws >= 0 ? windGauge.aws.toFixed(1)+" kt" : "-- kt",
                                 cx, cy + r*0.55)
                    ctx.fillStyle = "#9a9a3a"; ctx.font = "13px sans-serif"
                    ctx.fillText("AWA", cx, cy + r*0.74)
                }
            }
            // TWS | TWA numerics under the wind rose.
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    Label {
                        text: hudPanel.nav && hudPanel.nav.tws >= 0
                              ? hudPanel.nav.tws.toFixed(1) : "--"
                        color:"#e8e8e8"; font.pointSize: 22; Layout.alignment: Qt.AlignHCenter
                    }
                    Label { text: "TWS  kt"; color:"#808890"; font.pointSize: 10; Layout.alignment: Qt.AlignHCenter }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label {
                        text: hudPanel.nav && hudPanel.nav.twaValid
                              ? hudPanel.nav.twa.toFixed(0) : "--"
                        color:"#e8e8e8"; font.pointSize: 22; Layout.alignment: Qt.AlignHCenter
                    }
                    Label { text: "TWA  °"; color:"#808890"; font.pointSize: 10; Layout.alignment: Qt.AlignHCenter }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
