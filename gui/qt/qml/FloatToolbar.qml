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

// Master toolbar -- the persistent vertical control column (mirrors OpenCPN's
// floating master toolbar). Drives the `chart` context object / config
// singletons directly; tools that open a sibling window/drawer are surfaced as
// signals wired up in Main.qml.
//
// Single-backdrop flyouts (see MuiBar.qml): a flyout is an animated BULGE of the
// toolbar's OWN backdrop -- the pill and the bulge are drawn as ONE shape (one
// translucent fill, one border, concave fillets), so it grows out of the bar.
// Being a vertical bar, the bulge grows SIDEWAYS (right). The colour flyout is a
// ROTATION (the bar shows the active scheme, the bulge shows the other two);
// future flyouts (e.g. GRIB) host their own content in the same bulge.
//
// Styling (keep MuiBar.qml in sync): translucency lives in the background
// colours' alpha (NOT opacity, which would dim the icons); button chips a touch
// less transparent than the panel; icon glyphs fully opaque.
Item {
    id: floatToolbar
    implicitWidth: barCol.implicitWidth + 2 * pad
    implicitHeight: barCol.implicitHeight + 2 * pad

    property real touchSize: 44
    property bool collapsed: false
    readonly property real pad: 4
    readonly property real flyGap: 2
    readonly property real panelAlpha: 1.0 - UIConfig.toolbarTransparency
    readonly property real buttonAlpha: panelAlpha + (1.0 - panelAlpha) * 0.4
    readonly property real borderAlpha: panelAlpha * 0.4

    signal optionsRequested()
    signal routeManagerRequested()
    signal aboutRequested()
    signal anchorWatchRequested()
    signal pluginPrefsRequested(string pluginName)

    // --- Flyout (single-backdrop sideways bulge) ---
    property string activeFlyout: ""   // "" | "color" (sticky: kept while closing)
    property bool flyoutShown: false
    property real prog: 0
    onFlyoutShownChanged: prog = flyoutShown ? 1 : 0
    Behavior on prog { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
    function openFlyout(id) { activeFlyout = id; flyoutShown = true }
    function closeFlyout() { flyoutShown = false }

    // Colour scheme rotation: the bar shows the active scheme; the flyout shows
    // the other two (day / dusk / night).
    readonly property var schemeGlyphs: ["☀️", "🌆", "🌙"]
    readonly property var schemeTips: [qsTr("Color scheme: Day"),
                                       qsTr("Color scheme: Dusk"),
                                       qsTr("Color scheme: Night")]
    readonly property var schemeOthers:
        [0, 1, 2].filter(function(i) { return i !== chart.colorScheme })

    // Colour bulge dimensions (two scheme chips in a row).
    readonly property real colBulgeW: 2 * touchSize + 3 * flyGap
    readonly property real colBulgeH: touchSize + 2 * flyGap
    readonly property real colorCy: pad + colorTool.y + colorTool.height / 2

    // Plugin flyout (e.g. GRIB): the bulge hosts the plugin's QML content,
    // sized to it, at the plugin tool's y. Set when a plugin tool is held.
    property real pluginFlyoutCy: 0
    property url pluginFlyoutSource: ""
    property var pluginFlyoutContext: null
    property string pluginFlyoutPlugin: ""
    readonly property real pluginBulgeW:
        pluginLoader.item ? pluginLoader.item.implicitWidth + 2 * flyGap : 0
    readonly property real pluginBulgeH:
        pluginLoader.item ? pluginLoader.item.implicitHeight + 2 * flyGap : 0
    function openPluginFlyout(cy, source, context, pluginName) {
        pluginFlyoutCy = cy
        pluginFlyoutSource = source
        pluginFlyoutContext = context
        pluginFlyoutPlugin = pluginName
        activeFlyout = "plugin"
        flyoutShown = true
    }

    // Active flyout geometry (extend here for more flyouts).
    readonly property real flyoutCy: activeFlyout === "color" ? colorCy
                                   : activeFlyout === "plugin" ? pluginFlyoutCy : 0
    readonly property real flyoutW: activeFlyout === "color" ? colBulgeW
                                  : activeFlyout === "plugin" ? pluginBulgeW : 0
    readonly property real flyoutH: activeFlyout === "color" ? colBulgeH
                                  : activeFlyout === "plugin" ? pluginBulgeH : 0

    // Reusable chip factory (shared by the bar AND the flyouts).
    component Tool: ToolButton {
        id: ctl
        font.pointSize: 16
        implicitWidth: floatToolbar.touchSize
        implicitHeight: floatToolbar.touchSize
        Layout.alignment: Qt.AlignHCenter
        ToolTip.visible: hovered && ToolTip.text.length > 0
        ToolTip.delay: 400
        background: Rectangle {
            radius: 4
            readonly property real a: floatToolbar.buttonAlpha
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

    // Auto-hide (Options > User Interface).
    HoverHandler {
        id: tbHover
        onHoveredChanged: if (hovered && UIConfig.autoHideToolbar)
                              floatToolbar.collapsed = false
    }
    Timer {
        interval: Math.max(1, UIConfig.autoHideTimeout) * 1000
        running: UIConfig.autoHideToolbar && !tbHover.hovered
        onTriggered: floatToolbar.collapsed = true
    }

    // The ONE backdrop: vertical pill plus the animated sideways bulge, a single
    // path with a single fill + border. Extends right to host the bulge; never
    // intercepts input.
    Canvas {
        id: backdrop
        enabled: false
        x: 0; y: 0
        width: floatToolbar.width + floatToolbar.flyoutW
        height: floatToolbar.height
        readonly property string key: [width, height, floatToolbar.prog,
                                       floatToolbar.panelAlpha, floatToolbar.borderAlpha,
                                       floatToolbar.flyoutCy, floatToolbar.flyoutW,
                                       floatToolbar.flyoutH].join(",")
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
            var barW = floatToolbar.width, barH = floatToolbar.height
            var rB = 6, rt = 4
            var prog = floatToolbar.prog
            ctx.beginPath()
            if (prog < 0.01 || floatToolbar.flyoutW <= 0) {
                pill(ctx, 0, 0, barW, barH, rB)
            } else {
                var f = 8 * prog, rte = rt * prog
                var bw = prog * floatToolbar.flyoutW
                var cy = floatToolbar.flyoutCy, bh = floatToolbar.flyoutH
                var by0 = cy - bh / 2, by1 = cy + bh / 2
                var bxR = barW + bw
                // When the flyout button is the FIRST/LAST tool, the bulge edge
                // meets the toolbar's own top/bottom edge -- there is no toolbar
                // beyond it, so go STRAIGHT (merge the edges) instead of curving
                // a concave inner fillet into thin air.
                var topMerge = by0 <= rB
                var botMerge = by1 >= barH - rB
                ctx.moveTo(rB, 0)
                if (topMerge) {
                    ctx.lineTo(bxR - rB, 0)
                    ctx.arcTo(bxR, 0, bxR, rB, rB)                               // bulge TR (outer)
                } else {
                    ctx.lineTo(barW - rB, 0); ctx.arcTo(barW, 0, barW, rB, rB)   // bar TR
                    ctx.lineTo(barW, by0 - f)
                    ctx.arc(barW + f, by0 - f, f, Math.PI, Math.PI / 2, true)    // top fillet
                    ctx.lineTo(bxR - rte, by0); ctx.arcTo(bxR, by0, bxR, by0 + rte, rte)  // bulge TR
                }
                if (botMerge) {
                    ctx.lineTo(bxR, barH - rB); ctx.arcTo(bxR, barH, bxR - rB, barH, rB)  // bulge BR (outer)
                    ctx.lineTo(rB, barH); ctx.arcTo(0, barH, 0, barH - rB, rB)   // bar BL
                } else {
                    ctx.lineTo(bxR, by1 - rte); ctx.arcTo(bxR, by1, bxR - rte, by1, rte)  // bulge BR
                    ctx.lineTo(barW + f, by1)
                    ctx.arc(barW + f, by1 + f, f, -Math.PI / 2, Math.PI, true)   // bottom fillet
                    ctx.lineTo(barW, barH - rB); ctx.arcTo(barW, barH, barW - rB, barH, rB)  // bar BR
                    ctx.lineTo(rB, barH); ctx.arcTo(0, barH, 0, barH - rB, rB)   // bar BL
                }
                ctx.lineTo(0, rB); ctx.arcTo(0, 0, rB, 0, rB)                    // bar TL
                ctx.closePath()
            }
            ctx.fillStyle = Qt.rgba(0.93, 0.93, 0.95, floatToolbar.panelAlpha)
            ctx.fill()
            ctx.lineWidth = 1
            ctx.strokeStyle = Qt.rgba(0, 0, 0, floatToolbar.borderAlpha)
            ctx.stroke()
        }
    }

    ColumnLayout {
        id: barCol
        z: 2  // above the click-away dismiss
        x: floatToolbar.pad; y: floatToolbar.pad
        spacing: 2

        // wx ID_MASTERTOGGLE: collapse/expand; the rest hide when collapsed.
        Tool {
            text: "☰"
            ToolTip.text: floatToolbar.collapsed ? qsTr("Show toolbar")
                                                 : qsTr("Hide toolbar")
            onClicked: floatToolbar.collapsed = !floatToolbar.collapsed
        }
        Tool {
            visible: !floatToolbar.collapsed
            text: "⚙️"; ToolTip.text: qsTr("Options")
            onClicked: floatToolbar.optionsRequested()
        }
        Tool {
            visible: !floatToolbar.collapsed
            text: "✚"; checkable: true
            checked: (root.activeChart || chart).routeBuildMode
            ToolTip.text: qsTr("Create route  (left-click adds points, right-click finishes)")
            onClicked: (root.activeChart || chart).routeBuildMode = checked
        }
        Tool {
            visible: !floatToolbar.collapsed
            text: "📋"; ToolTip.text: qsTr("Route && mark manager")
            onClicked: floatToolbar.routeManagerRequested()
        }
        Tool {
            visible: !floatToolbar.collapsed
            text: "👣"; checkable: true
            checked: chart.trackRecording
            ToolTip.text: qsTr("Record own-ship track")
            onClicked: chart.trackRecording = checked
        }
        Tool {
            visible: !floatToolbar.collapsed
            text: "🌊"; checkable: true
            checked: DisplayConfig.showTides
            ToolTip.text: qsTr("Show tides && currents")
            onClicked: DisplayConfig.showTides = checked
        }
        // Time bar toggle (P3.14 decoupling): pin the bottom time scrubber. The
        // bar also auto-shows whenever tides or GRIB need it (barVisible), so
        // this checkbox reflects the user's explicit pin only.
        Tool {
            visible: !floatToolbar.collapsed
            text: "🕒"; checkable: true
            checked: TimeController.pinned
            ToolTip.text: qsTr("Show time bar")
            onClicked: TimeController.pinned = checked
        }
        // wx ID_COLSCHEME: the button shows the CURRENT scheme; tap cycles, hold
        // (or right-click) opens the bulge with the other two.
        Tool {
            id: colorTool
            visible: !floatToolbar.collapsed
            text: floatToolbar.schemeGlyphs[chart.colorScheme]
            ToolTip.text: floatToolbar.schemeTips[chart.colorScheme]
            onClicked: {
                if (floatToolbar.flyoutShown) { floatToolbar.closeFlyout(); return }
                chart.colorScheme = (chart.colorScheme + 1) % 3
            }
            onPressAndHold: floatToolbar.openFlyout("color")
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: floatToolbar.openFlyout("color")
            }
        }
        Tool {
            visible: !floatToolbar.collapsed
            text: "🖨️"; ToolTip.text: qsTr("Print chart (not yet implemented)")
        }
        Tool {
            visible: !floatToolbar.collapsed
            text: "ℹ️"; ToolTip.text: qsTr("About OpenCPN-NG")
            onClicked: floatToolbar.aboutRequested()
        }
        Tool {
            visible: !floatToolbar.collapsed
            text: "⚓"; ToolTip.text: qsTr("Anchor watch")
            highlighted: chart.alerts.anchorSet
            onClicked: floatToolbar.anchorWatchRequested()
        }
        Tool {
            visible: !floatToolbar.collapsed
            text: "🛟"; ToolTip.text: qsTr("Drop MOB marker")
            onClicked: chart.dropMob()
        }

        // Plugin toolbar actions (wx INSTALLS_TOOLBAR_TOOL parity).
        Repeater {
            model: chart.pluginRegistry.toolbarActions
            delegate: Tool {
                required property var modelData
                required property int index
                visible: !floatToolbar.collapsed
                text: modelData.glyph
                ToolTip.text: modelData.tooltip
                checkable: modelData.checkable === true
                checked: modelData.checkable === true &&
                         (chart.pluginRegistry.toolbarStateSerial,
                          chart.pluginRegistry.toolbarActionChecked(index))
                onClicked: chart.pluginRegistry.triggerToolbarAction(index)
                function secondary() {
                    if (modelData.flyoutSource !== undefined
                            && String(modelData.flyoutSource).length > 0)
                        floatToolbar.openPluginFlyout(
                            floatToolbar.pad + y + height / 2,
                            modelData.flyoutSource, modelData.flyoutContext,
                            modelData.pluginName)
                    else if (modelData.hasFlyout)
                        chart.pluginRegistry.triggerToolbarLongPress(index)
                    else
                        floatToolbar.pluginPrefsRequested(modelData.pluginName)
                }
                onPressAndHold: secondary()
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: parent.secondary()
                }
            }
        }
    }

    // Click-away dismiss: covers the parent while a flyout is open, below the
    // bar buttons (z) so they stay live, and below the flyout content.
    Item {
        z: 1
        visible: floatToolbar.flyoutShown
        x: -floatToolbar.x; y: -floatToolbar.y
        width: floatToolbar.parent ? floatToolbar.parent.width : 0
        height: floatToolbar.parent ? floatToolbar.parent.height : 0
        TapHandler { onTapped: floatToolbar.closeFlyout() }
    }

    // Colour flyout content (rotation): the two non-active schemes, in the bulge.
    // Slides out of the bar and fades in with `prog`.
    RowLayout {
        id: colorFlyoutRow
        z: 3
        visible: floatToolbar.activeFlyout === "color" && floatToolbar.prog > 0.01
        opacity: floatToolbar.prog
        spacing: floatToolbar.flyGap
        x: floatToolbar.width + floatToolbar.flyGap
           - (1 - floatToolbar.prog) * floatToolbar.colBulgeW
        y: floatToolbar.colorCy - height / 2
        Repeater {
            model: floatToolbar.schemeOthers
            Tool {
                required property var modelData
                text: floatToolbar.schemeGlyphs[modelData]
                ToolTip.text: floatToolbar.schemeTips[modelData]
                onClicked: {
                    chart.colorScheme = modelData;
                    floatToolbar.closeFlyout();
                }
            }
        }
    }

    // Plugin flyout content (e.g. GRIB toggles), hosted in the bulge. Loaded on
    // demand from the plugin's registered QML; slides out + fades with `prog`.
    Loader {
        id: pluginLoader
        z: 3
        active: floatToolbar.activeFlyout === "plugin"
        visible: active && floatToolbar.prog > 0.01
        opacity: floatToolbar.prog
        source: floatToolbar.pluginFlyoutSource
        x: floatToolbar.width + floatToolbar.flyGap
           - (1 - floatToolbar.prog) * floatToolbar.pluginBulgeW
        y: floatToolbar.pluginFlyoutCy - (item ? item.implicitHeight : 0) / 2
        onLoaded: {
            if (!item) return
            item.pluginContext = floatToolbar.pluginFlyoutContext
            if (item.chipSize !== undefined) item.chipSize = floatToolbar.touchSize
            if (item.settingsRequested)
                item.settingsRequested.connect(function() {
                    floatToolbar.pluginPrefsRequested(floatToolbar.pluginFlyoutPlugin)
                })
            if (item.closeRequested)
                item.closeRequested.connect(function() { floatToolbar.closeFlyout() })
        }
    }

    // Drag the whole toolbar; clamp within the parent (the chart overlay).
    DragHandler {
        target: floatToolbar
        xAxis.minimum: 0
        xAxis.maximum: floatToolbar.parent
                       ? floatToolbar.parent.width - floatToolbar.width : 0
        yAxis.minimum: 0
        yAxis.maximum: floatToolbar.parent
                       ? floatToolbar.parent.height - floatToolbar.height : 0
    }
}
