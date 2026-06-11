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

// Canvas right-click menus (P3.18, wx CanvasMenuHandler parity): the general
// chart menu plus focused per-object menus for a route, a mark, and a route
// node in edit mode. ChartCanvas hit-tests the click and emits one of the
// four *MenuRequested signals with item-local coordinates; popup(x, y) is
// relative to this Item, which Main.qml parents to the chart at (0, 0).
// Sibling dialogs (object query, mark editor, route details) are reached via
// the signals below, wired in Main.qml.
Item {
    id: menus

    // P6.1 focused-canvas: which pane these menus act on (default: the
    // primary chart; the second pane instantiates its own copy).
    property var targetChart: chart

    signal objectQueryRequested()
    signal newMarkRequested()
    signal editMarkRequested(string guid)
    signal routeDetailsRequested(int routeIndex)
    signal aisTargetListRequested()
    signal fullScreenRequested()
    signal sendRouteToPeerRequested(int routeIndex)
    signal sendMarkToPeerRequested(string guid, string name)
    signal sendTrackToPeerRequested(string guid, string name)
    signal sendRouteToGpsRequested(int routeIndex)
    signal sendMarkToGpsRequested(string guid, string name)

    // --- General canvas menu (right-click on open water / chart). ---------
    Menu {
        id: chartContextMenu
        MenuItem {
            text: qsTr("Undo")
            enabled: targetChart.canUndo
            onTriggered: targetChart.undo()
        }
        MenuItem {
            text: qsTr("Redo")
            enabled: targetChart.canRedo
            onTriggered: targetChart.redo()
        }
        MenuSeparator {}
        MenuItem {
            // wx ID_DEF_MENU_GOTO_HERE: temp route from the fix to here,
            // activated at once, deleted on arrival.
            text: qsTr("Navigate to here")
            enabled: targetChart.navState && targetChart.navState.ownShipValid
            onTriggered: targetChart.navigateToHere()
        }
        MenuItem {
            text: qsTr("Zero XTE")
            visible: targetChart.routeFollower.active
            height: visible ? implicitHeight : 0
            onTriggered: targetChart.zeroXte()
        }
        MenuItem {
            text: qsTr("Object query here")
            onTriggered: {
                targetChart.queryObjectsHere()
                menus.objectQueryRequested()
            }
        }
        MenuItem {
            text: qsTr("Center view here")
            onTriggered: targetChart.centerViewHere()
        }
        MenuSeparator {}
        // Chart controls (P3.18 tier 3, wx canvas_menu.cpp:441-611).
        MenuItem { text: qsTr("Scale in");  onTriggered: targetChart.zoomIn() }
        MenuItem { text: qsTr("Scale out"); onTriggered: targetChart.zoomOut() }
        Menu {
            title: qsTr("Chart orientation")
            MenuItem {
                text: qsTr("North-Up"); checkable: true
                checked: DisplayConfig.navMode === 0
                onTriggered: DisplayConfig.navMode = 0
            }
            MenuItem {
                text: qsTr("Course-Up"); checkable: true
                checked: DisplayConfig.navMode === 1
                onTriggered: DisplayConfig.navMode = 1
            }
            MenuItem {
                text: qsTr("Head-Up"); checkable: true
                checked: DisplayConfig.navMode === 2
                onTriggered: DisplayConfig.navMode = 2
            }
        }
        MenuItem {
            text: qsTr("Toggle full screen")
            onTriggered: menus.fullScreenRequested()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Create route")
            onTriggered: targetChart.routeBuildMode = true
        }
        MenuItem {
            text: qsTr("Drop mark here")
            onTriggered: menus.newMarkRequested()  // dialog uses the ctx point
        }
        MenuItem {
            text: targetChart.measureActive ? qsTr("Measure off") : qsTr("Measure")
            onTriggered: targetChart.measureActive ? targetChart.stopMeasure()
                                             : targetChart.startMeasure()
        }
        MenuItem {
            text: qsTr("Paste KML")
            onTriggered: targetChart.pasteKmlFromClipboard()
        }
        MenuSeparator {}
        MenuItem {
            // Test ship (P3.16): drop a synthetic GPS here and grab the
            // keyboard so the cursor keys steer it straight away.
            text: targetChart.simShip.active ? qsTr("Move test ship here")
                                       : qsTr("Place test ship here")
            onTriggered: {
                targetChart.placeSimShipHere()
                simKeyHandler.forceActiveFocus()
            }
        }
    
    // Plugin context-menu items (wx INSTALLS_CONTEXTMENU_ITEMS parity):
    // appended to the general chart menu with the click position.
    Instantiator {
        model: targetChart.pluginRegistry.contextMenuItems
        delegate: MenuItem {
            required property var modelData
            required property int index
            text: modelData.label
            onTriggered: targetChart.pluginRegistry.triggerContextMenuItem(
                             index, targetChart.markDropLat(),
                             targetChart.markDropLon())
        }
        onObjectAdded: (i, o) => chartContextMenu.addItem(o)
        onObjectRemoved: (i, o) => chartContextMenu.removeItem(o)
    }
    }

    // --- Route menu (right-click a route's line or node). -----------------
    Menu {
        id: routeMenu
        property int routeIndex: -1
        property bool isActive: false
        property bool canInsert: false

        MenuItem {
            text: routeMenu.isActive ? qsTr("Deactivate") : qsTr("Activate")
            onTriggered: routeMenu.isActive
                         ? targetChart.deactivateRoute()
                         : targetChart.activateRoute(routeMenu.routeIndex)
        }
        MenuItem {
            text: qsTr("Activate next waypoint")
            visible: routeMenu.isActive
            height: visible ? implicitHeight : 0
            onTriggered: targetChart.skipWaypoint()
        }
        MenuItem {
            text: qsTr("Zero XTE")
            visible: routeMenu.isActive
            height: visible ? implicitHeight : 0
            onTriggered: targetChart.zeroXte()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Insert waypoint here")
            enabled: routeMenu.canInsert
            onTriggered: targetChart.insertRoutePointAtMenu()
        }
        MenuItem {
            text: qsTr("Append waypoints")
            onTriggered: targetChart.appendToRoute(routeMenu.routeIndex)
        }
        MenuItem {
            text: qsTr("Split at this leg")
            enabled: routeMenu.canInsert && !routeMenu.isActive
            onTriggered: targetChart.splitRouteAtMenu()
        }
        MenuItem {
            text: qsTr("Edit route points")
            onTriggered: targetChart.editRoute(routeMenu.routeIndex)
        }
        MenuItem {
            text: qsTr("Reverse")
            onTriggered: targetChart.reverseRoute(routeMenu.routeIndex)
        }
        MenuItem {
            text: qsTr("Details…")
            onTriggered: menus.routeDetailsRequested(routeMenu.routeIndex)
        }
        MenuItem {
            text: qsTr("Copy as KML")
            onTriggered: targetChart.copyRouteAsKml(routeMenu.routeIndex)
        }
        MenuItem {
            text: qsTr("Send to peer…")
            onTriggered: menus.sendRouteToPeerRequested(routeMenu.routeIndex)
        }
        MenuItem {
            text: qsTr("Send to GPS…")
            onTriggered: menus.sendRouteToGpsRequested(routeMenu.routeIndex)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Delete route")
            onTriggered: confirmDelete.ask(
                qsTr("Delete this route?"),
                function() { targetChart.deleteRoute(routeMenu.routeIndex) })
        }
    }

    // --- Mark menu (right-click a free mark). ------------------------------
    Menu {
        id: markMenu
        property string guid: ""
        property string markName: ""

        MenuItem {
            // wx ID_WP_MENU_GOTO: temp route from the fix to this mark.
            text: markMenu.markName.length > 0
                  ? qsTr("Navigate to \"%1\"").arg(markMenu.markName)
                  : qsTr("Navigate to this mark")
            enabled: targetChart.navState && targetChart.navState.ownShipValid
            onTriggered: targetChart.navigateToWaypoint(markMenu.guid)
        }
        MenuItem {
            text: qsTr("Edit mark…")
            onTriggered: menus.editMarkRequested(markMenu.guid)
        }
        MenuItem {
            text: qsTr("Copy as KML")
            onTriggered: targetChart.copyMarkAsKml(markMenu.guid)
        }
        MenuItem {
            text: qsTr("Send to peer…")
            onTriggered: menus.sendMarkToPeerRequested(markMenu.guid,
                                                       markMenu.markName)
        }
        MenuItem {
            text: qsTr("Send to GPS…")
            onTriggered: menus.sendMarkToGpsRequested(markMenu.guid,
                                                      markMenu.markName)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Delete mark")
            onTriggered: confirmDelete.ask(
                qsTr("Delete mark \"%1\"?").arg(markMenu.markName),
                function() { targetChart.deleteMark(markMenu.guid) })
        }
    }

    // --- AIS target menu (right-click a target). ---------------------------
    Menu {
        id: aisMenu
        property int mmsi: 0
        property string targetName: ""

        MenuItem {
            // Opens the same info popup a left-click select does.
            text: qsTr("Target query")
            onTriggered: targetChart.selectAisTarget(aisMenu.mmsi)
        }
        MenuItem {
            text: qsTr("Center view on target")
            onTriggered: targetChart.centerOnAis(aisMenu.mmsi)
        }
        MenuItem {
            text: targetChart.aisTrailEnabled(aisMenu.mmsi) ? qsTr("Hide target track")
                                                      : qsTr("Show target track")
            onTriggered: targetChart.setAisTrail(aisMenu.mmsi,
                                           !targetChart.aisTrailEnabled(aisMenu.mmsi))
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Target list…")
            onTriggered: menus.aisTargetListRequested()
        }
        MenuItem {
            text: qsTr("Copy MMSI")
            onTriggered: targetChart.copyToClipboard(String(aisMenu.mmsi))
        }
    }

    // --- Track menu (right-click a track's line, P3.18 tier 3). -----------
    Menu {
        id: trackMenu
        property string guid: ""
        property string trackName: ""

        MenuItem {
            text: qsTr("Hide track")
            onTriggered: targetChart.setTrackVisible(trackMenu.guid, false)
        }
        MenuItem {
            text: qsTr("Zoom to track")
            onTriggered: targetChart.showTrack(trackMenu.guid)
        }
        MenuItem {
            text: qsTr("Copy as KML")
            onTriggered: targetChart.copyTrackAsKml(trackMenu.guid)
        }
        MenuItem {
            text: qsTr("Send to peer…")
            onTriggered: menus.sendTrackToPeerRequested(trackMenu.guid,
                                                        trackMenu.trackName)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Delete track")
            onTriggered: confirmDelete.ask(
                qsTr("Delete track \"%1\"?").arg(trackMenu.trackName),
                function() { targetChart.deleteTrack(trackMenu.guid) })
        }
    }

    // --- Route-node menu (right-click a node of the route being edited). --
    Menu {
        id: routeNodeMenu
        MenuItem {
            text: qsTr("Delete point")
            onTriggered: targetChart.deleteRoutePointAtMenu()
        }
        MenuItem {
            text: qsTr("Delete route")
            onTriggered: confirmDelete.ask(
                qsTr("Delete the route being edited?"),
                function() { targetChart.deleteSelectedRoute() })
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Finish editing")
            onTriggered: targetChart.clearRouteSelection()
        }
    }

    // Delete confirmation (P3.6, honours Options > Routes & Marks).
    ConfirmDialog { id: confirmDelete }

    Connections {
        target: targetChart
        function onContextMenuRequested(x, y) {
            chartContextMenu.popup(x, y)
        }
        function onRouteMenuRequested(x, y, routeIndex, isActive, canInsert) {
            routeMenu.routeIndex = routeIndex
            routeMenu.isActive = isActive
            routeMenu.canInsert = canInsert
            routeMenu.popup(x, y)
        }
        function onMarkMenuRequested(x, y, guid, name) {
            markMenu.guid = guid
            markMenu.markName = name
            markMenu.popup(x, y)
        }
        function onAisMenuRequested(x, y, mmsi, name) {
            aisMenu.mmsi = mmsi
            aisMenu.targetName = name
            aisMenu.popup(x, y)
        }
        function onTrackMenuRequested(x, y, guid, name) {
            trackMenu.guid = guid
            trackMenu.trackName = name
            trackMenu.popup(x, y)
        }
        function onRouteNodeMenuRequested(x, y) {
            routeNodeMenu.popup(x, y)
        }
    }
}
