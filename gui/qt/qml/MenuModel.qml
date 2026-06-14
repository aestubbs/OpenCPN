/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

import QtQuick
import QtQuick.Window
import opencpn.qt

// --- Single source of truth for the application menu (wx RegisterGlobalMenuItems
//     parity) ---
// macOS draws the NATIVE global bar at the top of the screen (AppMenuBar.qml,
// Qt.labs.platform); every other platform draws an in-window bar at the top of
// the window (AppMenuBarInWindow.qml, QtQuick.Controls). Those are two
// different, incompatible menu frameworks, so the visual tree can't be one
// component -- but the DEFINITION can. Both bars build themselves from the
// `menus` model below, so labels, shortcuts, checked-state, actions, order and
// separators all live HERE, once.
//
// Model shape:
//   menus: [ { title, items: [ item, ... ] }, ... ]
//   item:  { separator: true }
//        | { text, shortcut?, checkable?, checked?: ()->bool, triggered: ()->void }
// `checked` is a function so the bars can wrap it in a live binding (the tick
// then tracks the underlying chart/config property reactively). `triggered`
// performs the action. Both close over this object's injected `chart` / window
// references, so they keep working when `chart` switches (split view).
QtObject {
    id: model

    // Wired by the shell (Main.qml): the active canvas + the windows.
    property var chart
    property var rootWindow
    property var optionsWin
    property var aisTargetList
    property var dataMonitor
    property var aboutWin
    property var drawerRef

    readonly property var menus: [
        {
            title: qsTr("&File"),
            items: [
                { text: qsTr("Quit"), shortcut: "Ctrl+Q",
                  triggered: function() { Qt.quit() } },
            ]
        },
        {
            title: qsTr("&Navigate"),
            items: [
                { text: qsTr("Auto Follow"), shortcut: "Ctrl+A", checkable: true,
                  checked: function() { return model.chart ? model.chart.followOwnShip : false },
                  triggered: function() { if (model.chart) model.chart.followOwnShip = !model.chart.followOwnShip } },
                { text: qsTr("Enable Tracking"), checkable: true,
                  checked: function() { return model.chart ? model.chart.trackRecording : false },
                  triggered: function() { if (model.chart) model.chart.trackRecording = !model.chart.trackRecording } },
                { separator: true },
                { text: qsTr("North Up Mode"), checkable: true,
                  checked: function() { return DisplayConfig.navMode === 0 },
                  triggered: function() { DisplayConfig.navMode = 0 } },
                { text: qsTr("Course Up Mode"), checkable: true,
                  checked: function() { return DisplayConfig.navMode === 1 },
                  triggered: function() { DisplayConfig.navMode = 1 } },
                { text: qsTr("Head Up Mode"), checkable: true,
                  checked: function() { return DisplayConfig.navMode === 2 },
                  triggered: function() { DisplayConfig.navMode = 2 } },
                { separator: true },
                { text: qsTr("Zoom In"), shortcut: "Alt++",
                  triggered: function() { if (model.chart) model.chart.zoomIn() } },
                { text: qsTr("Zoom Out"), shortcut: "Alt+-",
                  triggered: function() { if (model.chart) model.chart.zoomOut() } },
                { separator: true },
                { text: qsTr("Larger Scale Chart"), shortcut: "Ctrl+Left",
                  triggered: function() { if (model.chart) model.chart.scaleChartStep(1) } },
                { text: qsTr("Smaller Scale Chart"), shortcut: "Ctrl+Right",
                  triggered: function() { if (model.chart) model.chart.scaleChartStep(-1) } },
            ]
        },
        {
            title: qsTr("&View"),
            items: [
                { text: qsTr("Show Chart Outlines"), shortcut: "Alt+O", checkable: true,
                  checked: function() { return DisplayConfig.showChartOutlines },
                  triggered: function() { DisplayConfig.showChartOutlines = !DisplayConfig.showChartOutlines } },
                { text: qsTr("Show Chart Bar"), shortcut: "Ctrl+B", checkable: true,
                  checked: function() { return UIConfig.showChartBar },
                  triggered: function() { UIConfig.showChartBar = !UIConfig.showChartBar } },
                { separator: true },
                { text: qsTr("Show ENC Text"), shortcut: "Alt+T", checkable: true,
                  checked: function() { return model.chart ? model.chart.showText : false },
                  triggered: function() { if (model.chart) model.chart.showText = !model.chart.showText } },
                { text: qsTr("Show ENC Lights"), shortcut: "Alt+L", checkable: true,
                  checked: function() { return model.chart ? model.chart.showLights : false },
                  triggered: function() { if (model.chart) model.chart.showLights = !model.chart.showLights } },
                { text: qsTr("Show ENC Soundings"), shortcut: "Alt+S", checkable: true,
                  checked: function() { return model.chart ? model.chart.showSoundings : false },
                  triggered: function() { if (model.chart) model.chart.showSoundings = !model.chart.showSoundings } },
                { text: qsTr("Show ENC Anchoring Info"), shortcut: "Alt+A", checkable: true,
                  checked: function() { return model.chart ? model.chart.showEncAnchoring : true },
                  triggered: function() { if (model.chart) model.chart.showEncAnchoring = !model.chart.showEncAnchoring } },
                { text: qsTr("Show ENC Data Quality"), shortcut: "Alt+U", checkable: true,
                  checked: function() { return ChartConfig.dataQuality },
                  triggered: function() { ChartConfig.dataQuality = !ChartConfig.dataQuality } },
                { text: qsTr("Show Navobjects"), shortcut: "Alt+V", checkable: true,
                  checked: function() { return model.chart ? model.chart.showRoutes : false },
                  triggered: function() {
                      if (!model.chart) return
                      var on = !model.chart.showRoutes
                      model.chart.showRoutes = on
                      model.chart.showTracks = on
                  } },
                { separator: true },
                // One seam covers both (the tide layer draws currents too).
                { text: qsTr("Show Tides && Currents"), checkable: true,
                  checked: function() { return DisplayConfig.showTides },
                  triggered: function() { DisplayConfig.showTides = !DisplayConfig.showTides } },
                { separator: true },
                { text: qsTr("Change Color Scheme"), shortcut: "Alt+C",
                  triggered: function() { if (model.chart) model.chart.colorScheme = (model.chart.colorScheme + 1) % 3 } },
                { separator: true },
                { text: qsTr("Toggle Full Screen"),
                  triggered: function() {
                      if (!model.rootWindow) return
                      if (model.rootWindow.visibility === Window.FullScreen)
                          model.rootWindow.showNormal()
                      else
                          model.rootWindow.showFullScreen()
                  } },
            ]
        },
        {
            title: qsTr("&AIS"),
            items: [
                { text: qsTr("Show AIS Targets"), checkable: true,
                  checked: function() { return AisConfig.showTargets },
                  triggered: function() { AisConfig.showTargets = !AisConfig.showTargets } },
                { text: qsTr("Hide Moored AIS Targets"), checkable: true,
                  checked: function() { return AisConfig.hideMoored },
                  triggered: function() { AisConfig.hideMoored = !AisConfig.hideMoored } },
                { text: qsTr("Show AIS Target Tracks"), checkable: true,
                  checked: function() { return AisConfig.showTargetTracks },
                  triggered: function() { AisConfig.showTargetTracks = !AisConfig.showTargetTracks } },
                { separator: true },
                { text: qsTr("Show CPA Alert Dialogs"), checkable: true,
                  checked: function() { return AisConfig.cpaAlert },
                  triggered: function() { AisConfig.cpaAlert = !AisConfig.cpaAlert } },
                { text: qsTr("Sound CPA Alarms"), checkable: true,
                  checked: function() { return AisConfig.cpaAlertSound },
                  triggered: function() { AisConfig.cpaAlertSound = !AisConfig.cpaAlertSound } },
                { separator: true },
                { text: qsTr("AIS Target List…"),
                  triggered: function() {
                      if (!model.aisTargetList) return
                      model.aisTargetList.show()
                      model.aisTargetList.raise()
                  } },
            ]
        },
        {
            title: qsTr("&Tools"),
            items: [
                { text: qsTr("Data Monitor"), shortcut: "Alt+E",
                  triggered: function() {
                      if (!model.dataMonitor) return
                      model.dataMonitor.show()
                      model.dataMonitor.raise()
                  } },
                { text: qsTr("Measure Distance"), shortcut: "Alt+M",
                  triggered: function() { if (model.chart) model.chart.startMeasure() } },
                { separator: true },
                { text: qsTr("Route && Mark Manager…"),
                  triggered: function() { if (model.drawerRef) model.drawerRef.open() } },
                { text: qsTr("Create Route"), shortcut: "Ctrl+R",
                  triggered: function() { if (model.chart) model.chart.routeBuildMode = true } },
                { separator: true },
                { text: qsTr("Drop Mark at Boat"), shortcut: "Ctrl+O",
                  triggered: function() { if (model.chart) model.chart.dropMarkAtBoat() } },
                { text: qsTr("Drop Mark at Cursor"), shortcut: "Ctrl+M",
                  triggered: function() { if (model.chart) model.chart.dropMarkAtCursor() } },
                { separator: true },
                { text: qsTr("Drop MOB Marker"),
                  triggered: function() { if (model.chart) model.chart.dropMob() } },
                { separator: true },
                { text: qsTr("Options…"), shortcut: "Ctrl+,",
                  triggered: function() {
                      if (!model.optionsWin) return
                      model.optionsWin.show()
                      model.optionsWin.raise()
                  } },
            ]
        },
        {
            title: qsTr("&Help"),
            items: [
                { text: qsTr("About OpenCPN"),
                  triggered: function() {
                      if (!model.aboutWin) return
                      model.aboutWin.show()
                      model.aboutWin.raise()
                  } },
                { text: qsTr("OpenCPN Help"),
                  triggered: function() { Qt.openUrlExternally(
                      "https://opencpn-manuals.github.io/main/opencpn/") } },
            ]
        },
    ]
}
