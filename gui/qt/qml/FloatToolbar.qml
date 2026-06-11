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
// floating master toolbar). Extracted from Main.qml (P3.17). Drives the `chart`
// context object / config singletons directly; tools that open a sibling
// window/drawer (Options, Route manager, Data monitor, About, Anchor watch) are
// surfaced as signals wired up in Main.qml. Layout per the agreed toolbar spec
// (Docs/QT_MIGRATION_TASKS.md, 2026-06-09): Tides + Anchor relocated here from
// the MUI bar, MOB icon fixed to the life-buoy.
//
// Styling (keep MuiBar.qml in sync): transparency lives in the background
// *colours' alpha* (NOT Pane.opacity, which would dim the icons too). The panel
// is translucent (alpha from UIConfig.toolbarTransparency) with a 1px opaque
// black border; button chips are a touch less transparent than the panel; icon
// glyphs are fully opaque.
Pane {
    id: floatToolbar
    padding: 4

    // Per-tool touch size (bound from the window's touchSize).
    property real touchSize: 44
    // Auto-hide collapse state (was root.toolbarCollapsed; only used here).
    property bool collapsed: false
    // Panel translucency, user-controlled (Options > User Interface).
    readonly property real panelAlpha: 1.0 - UIConfig.toolbarTransparency
    // Buttons are a touch less transparent than the panel: 40% of the way from
    // the panel's alpha toward opaque (never clamping to fully opaque).
    readonly property real buttonAlpha: panelAlpha + (1.0 - panelAlpha) * 0.4
    // The 1px border is a subtle hairline -- much more transparent than the
    // panel (black reads high-contrast, so keep its alpha well down).
    readonly property real borderAlpha: panelAlpha * 0.4

    signal optionsRequested()
    signal routeManagerRequested()
    signal aboutRequested()
    signal anchorWatchRequested()
    // Long-press on a plugin tool: open that plugin's preferences.
    signal pluginPrefsRequested(string pluginName)

    background: Rectangle {
        color: Qt.rgba(0.93, 0.93, 0.95, floatToolbar.panelAlpha)
        border.color: Qt.rgba(0, 0, 0, floatToolbar.borderAlpha)
        border.width: 1
        radius: 6
    }

    // Auto-hide (Options > User Interface): collapse to the toggle after a
    // period of no hover; pointing at it expands it again.
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

    // Reusable vertical tool factory: a translucent chip (slightly less
    // transparent than the panel) with a fully-opaque icon glyph. Inert tools
    // just omit an onClicked handler.
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

    ColumnLayout {
        spacing: 2

        // wx ID_MASTERTOGGLE: collapse/expand; the rest hide when collapsed.
        Tool {
            text: "☰"
            ToolTip.text: floatToolbar.collapsed ? qsTr("Show toolbar")
                                                 : qsTr("Hide toolbar")
            onClicked: floatToolbar.collapsed = !floatToolbar.collapsed
        }
        // wx ID_SETTINGS.
        Tool {
            visible: !floatToolbar.collapsed
            text: "⚙️"; ToolTip.text: qsTr("Options")
            onClicked: floatToolbar.optionsRequested()
        }
        // wx ID_MENU_ROUTE_NEW.
        Tool {
            visible: !floatToolbar.collapsed
            text: "✚"; checkable: true
            checked: (root.activeChart || chart).routeBuildMode
            ToolTip.text: qsTr("Create route  (left-click adds points, right-click finishes)")
            onClicked: (root.activeChart || chart).routeBuildMode = checked
        }
        // wx ID_ROUTEMANAGER.
        Tool {
            visible: !floatToolbar.collapsed
            text: "📋"; ToolTip.text: qsTr("Route && mark manager")
            onClicked: floatToolbar.routeManagerRequested()
        }
        // wx ID_TRACK.
        Tool {
            visible: !floatToolbar.collapsed
            text: "👣"; checkable: true
            checked: chart.trackRecording
            ToolTip.text: qsTr("Record own-ship track")
            onClicked: chart.trackRecording = checked
        }
        // Tides (moved here from the MUI bar).
        Tool {
            visible: !floatToolbar.collapsed
            text: "🌊"; checkable: true
            checked: DisplayConfig.showTides
            ToolTip.text: qsTr("Show tides && currents")
            onClicked: DisplayConfig.showTides = checked
        }
        // wx ID_COLSCHEME: cycle day/dusk/night.
        Tool {
            visible: !floatToolbar.collapsed
            text: "🌗"
            ToolTip.text: [qsTr("Color scheme: Day"),
                           qsTr("Color scheme: Dusk"),
                           qsTr("Color scheme: Night")][chart.colorScheme]
            onClicked: chart.colorScheme = (chart.colorScheme + 1) % 3
        }
        // wx ID_PRINT -- full chart printing is a separate feature; inert.
        Tool {
            visible: !floatToolbar.collapsed
            text: "🖨️"; ToolTip.text: qsTr("Print chart (not yet implemented)")
        }
        // (Data Monitor moved to Options > Connections, P3.22 -- it is a
        // connections-debugging tool, not a primary nav control.)
        // wx ID_ABOUT.
        Tool {
            visible: !floatToolbar.collapsed
            text: "ℹ️"; ToolTip.text: qsTr("About OpenCPN-NG")
            onClicked: floatToolbar.aboutRequested()
        }
        // Anchor watch (moved here from the MUI bar).
        Tool {
            visible: !floatToolbar.collapsed
            text: "⚓"; ToolTip.text: qsTr("Anchor watch")
            highlighted: chart.alerts.anchorSet
            onClicked: floatToolbar.anchorWatchRequested()
        }
        // wx ID_MOB: drop a man-overboard mark at the ship. Icon fixed to the
        // life-buoy (U+1F6DF, matching wx's red life-buoy; was the anchor glyph).
        Tool {
            visible: !floatToolbar.collapsed
            text: "🛟"; ToolTip.text: qsTr("Drop MOB marker")
            onClicked: chart.dropMob()
        }

        // Plugin toolbar actions (wx INSTALLS_TOOLBAR_TOOL parity),
        // styled like the native tools. Press-and-hold (or right-click)
        // opens the plugin's preferences, wx-style.
        Repeater {
            model: chart.pluginRegistry.toolbarActions
            delegate: Tool {
                required property var modelData
                required property int index
                visible: !floatToolbar.collapsed
                text: modelData.glyph
                ToolTip.text: modelData.tooltip
                onClicked: chart.pluginRegistry.triggerToolbarAction(index)
                onPressAndHold: floatToolbar.pluginPrefsRequested(
                                    modelData.pluginName)
                // wx parity: right-click the tool opens its preferences.
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: floatToolbar.pluginPrefsRequested(
                                  modelData.pluginName)
                }
            }
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
