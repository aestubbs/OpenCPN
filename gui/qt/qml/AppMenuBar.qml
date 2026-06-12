/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

import QtQuick
import Qt.labs.platform as Platform
import opencpn.qt

// --- Application menu bar (wx RegisterGlobalMenuItems parity) ---
// Qt.labs.platform renders the NATIVE menu bar (the macOS global bar),
// matching the wx app's Navigate / View / AIS / Tools / Help tree. Items
// bind two-way onto the same seams the toolbars and Options dialog use.
// wx items without a Qt seam yet are omitted (quilting is always-on by
// design; larger/smaller-scale chart, ENC anchoring info, and the AIS
// target/moored/track toggles pend their seams -- tracked in P3.23).
Platform.MenuBar {
    id: menuBar

    // Wired by the shell (Main.qml): the primary canvas + the windows.
    property var chart
    property var rootWindow
    property var optionsWin
    property var aisTargetList
    property var dataMonitor
    property var aboutWin
    property var drawerRef

    Platform.Menu {
        title: qsTr("&Navigate")
        Platform.MenuItem {
            text: qsTr("Auto Follow")
            shortcut: "Ctrl+A"
            checkable: true
            checked: menuBar.chart ? menuBar.chart.followOwnShip : false
            onTriggered: menuBar.chart.followOwnShip = checked
        }
        Platform.MenuItem {
            text: qsTr("Enable Tracking")
            checkable: true
            checked: menuBar.chart ? menuBar.chart.trackRecording : false
            onTriggered: menuBar.chart.trackRecording = checked
        }
        Platform.MenuSeparator { }
        Platform.MenuItemGroup { id: upModeGroup }
        Platform.MenuItem {
            text: qsTr("North Up Mode")
            checkable: true
            group: upModeGroup
            checked: DisplayConfig.navMode === 0
            onTriggered: DisplayConfig.navMode = 0
        }
        Platform.MenuItem {
            text: qsTr("Course Up Mode")
            checkable: true
            group: upModeGroup
            checked: DisplayConfig.navMode === 1
            onTriggered: DisplayConfig.navMode = 1
        }
        Platform.MenuItem {
            text: qsTr("Head Up Mode")
            checkable: true
            group: upModeGroup
            checked: DisplayConfig.navMode === 2
            onTriggered: DisplayConfig.navMode = 2
        }
        Platform.MenuSeparator { }
        Platform.MenuItem {
            text: qsTr("Zoom In")
            shortcut: "Alt++"
            onTriggered: menuBar.chart.zoomIn()
        }
        Platform.MenuItem {
            text: qsTr("Zoom Out")
            shortcut: "Alt+-"
            onTriggered: menuBar.chart.zoomOut()
        }
    }

    Platform.Menu {
        title: qsTr("&View")
        Platform.MenuItem {
            text: qsTr("Show Chart Outlines")
            shortcut: "Alt+O"
            checkable: true
            checked: DisplayConfig.showChartOutlines
            onTriggered: DisplayConfig.showChartOutlines = checked
        }
        Platform.MenuItem {
            text: qsTr("Show Chart Bar")
            shortcut: "Ctrl+B"
            checkable: true
            checked: UIConfig.showChartBar
            onTriggered: UIConfig.showChartBar = checked
        }
        Platform.MenuSeparator { }
        Platform.MenuItem {
            text: qsTr("Show ENC Text")
            shortcut: "Alt+T"
            checkable: true
            checked: menuBar.chart ? menuBar.chart.showText : false
            onTriggered: menuBar.chart.showText = checked
        }
        Platform.MenuItem {
            text: qsTr("Show ENC Lights")
            shortcut: "Alt+L"
            checkable: true
            checked: menuBar.chart ? menuBar.chart.showLights : false
            onTriggered: menuBar.chart.showLights = checked
        }
        Platform.MenuItem {
            text: qsTr("Show ENC Soundings")
            shortcut: "Alt+S"
            checkable: true
            checked: menuBar.chart ? menuBar.chart.showSoundings : false
            onTriggered: menuBar.chart.showSoundings = checked
        }
        Platform.MenuItem {
            text: qsTr("Show ENC Data Quality")
            shortcut: "Alt+U"
            checkable: true
            checked: ChartConfig.dataQuality
            onTriggered: ChartConfig.dataQuality = checked
        }
        Platform.MenuItem {
            text: qsTr("Show Navobjects")
            shortcut: "Alt+V"
            checkable: true
            checked: menuBar.chart ? menuBar.chart.showRoutes : false
            onTriggered: {
                menuBar.chart.showRoutes = checked
                menuBar.chart.showTracks = checked
            }
        }
        Platform.MenuSeparator { }
        Platform.MenuItem {
            // One seam covers both (the tide layer draws currents too).
            text: qsTr("Show Tides && Currents")
            checkable: true
            checked: DisplayConfig.showTides
            onTriggered: DisplayConfig.showTides = checked
        }
        Platform.MenuSeparator { }
        Platform.MenuItem {
            text: qsTr("Change Color Scheme")
            shortcut: "Alt+C"
            onTriggered: menuBar.chart.colorScheme =
                             (menuBar.chart.colorScheme + 1) % 3
        }
        Platform.MenuSeparator { }
        Platform.MenuItem {
            text: qsTr("Toggle Full Screen")
            onTriggered: menuBar.rootWindow.visibility === Window.FullScreen
                         ? menuBar.rootWindow.showNormal()
                         : menuBar.rootWindow.showFullScreen()
        }
    }

    Platform.Menu {
        title: qsTr("&AIS")
        Platform.MenuItem {
            text: qsTr("Show CPA Alert Dialogs")
            checkable: true
            checked: AisConfig.cpaAlert
            onTriggered: AisConfig.cpaAlert = checked
        }
        Platform.MenuItem {
            text: qsTr("Sound CPA Alarms")
            checkable: true
            checked: AisConfig.cpaAlertSound
            onTriggered: AisConfig.cpaAlertSound = checked
        }
        Platform.MenuSeparator { }
        Platform.MenuItem {
            text: qsTr("AIS Target List…")
            onTriggered: {
                menuBar.aisTargetList.show()
                menuBar.aisTargetList.raise()
            }
        }
    }

    Platform.Menu {
        title: qsTr("&Tools")
        Platform.MenuItem {
            text: qsTr("Data Monitor")
            shortcut: "Alt+E"
            onTriggered: {
                menuBar.dataMonitor.show()
                menuBar.dataMonitor.raise()
            }
        }
        Platform.MenuItem {
            text: qsTr("Measure Distance")
            shortcut: "Alt+M"
            onTriggered: menuBar.chart.startMeasure()
        }
        Platform.MenuSeparator { }
        Platform.MenuItem {
            text: qsTr("Route && Mark Manager…")
            onTriggered: menuBar.drawerRef.open()
        }
        Platform.MenuItem {
            text: qsTr("Create Route")
            shortcut: "Ctrl+R"
            onTriggered: menuBar.chart.routeBuildMode = true
        }
        Platform.MenuSeparator { }
        Platform.MenuItem {
            text: qsTr("Drop Mark at Boat")
            shortcut: "Ctrl+O"
            onTriggered: menuBar.chart.dropMarkAtBoat()
        }
        Platform.MenuItem {
            text: qsTr("Drop Mark at Cursor")
            shortcut: "Ctrl+M"
            onTriggered: menuBar.chart.dropMarkAtCursor()
        }
        Platform.MenuSeparator { }
        Platform.MenuItem {
            text: qsTr("Drop MOB Marker")
            onTriggered: menuBar.chart.dropMob()
        }
        Platform.MenuSeparator { }
        Platform.MenuItem {
            text: qsTr("Options…")
            shortcut: "Ctrl+,"
            onTriggered: {
                menuBar.optionsWin.show()
                menuBar.optionsWin.raise()
            }
        }
    }

    Platform.Menu {
        title: qsTr("&Help")
        Platform.MenuItem {
            text: qsTr("About OpenCPN")
            onTriggered: {
                menuBar.aboutWin.show()
                menuBar.aboutWin.raise()
            }
        }
        Platform.MenuItem {
            text: qsTr("OpenCPN Help")
            onTriggered: Qt.openUrlExternally(
                             "https://opencpn-manuals.github.io/main/opencpn/")
        }
    }
}
