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
import QtQuick.Dialogs
import QtQuick.Layouts
import opencpn.qt

// --- Route & mark manager: a left-edge drawer of route tiles (P3.7).
//     Non-modal + undimmed so the chart stays live behind it; a tile shows
//     the route name + stats, click zooms to its extent, and the ... menu
//     holds rename (inline) / duplicate / reverse / delete.
Drawer {
    id: routeDrawer

    // The application shell window (drawer sizing tracks it).
    required property var appWindow

    // Sibling dialogs are opened by Main.qml in response to these (same
    // pattern as FloatToolbar's window-opening signals).
    signal routeDetailsRequested(int index, string name)
    signal editMarkRequested(string guid, string name, string comment,
                             string icon)

    edge: Qt.LeftEdge
    width: 340
    height: appWindow.height
    modal: false
    dim: false

    readonly property var rl: chart.routeList

    // Transient GPX import/export feedback ("" = hidden).
    property string gpxStatus: ""
    Timer {
        id: gpxStatusTimer
        interval: 6000
        onTriggered: routeDrawer.gpxStatus = ""
    }
    function showGpxStatus(msg) {
        gpxStatus = msg
        gpxStatusTimer.restart()
    }

    // Delete confirmation (P3.6, honours Options > Routes & Marks).
    ConfirmDialog { id: drawerConfirmDelete }

    // --- GPX interchange (P3.19, wx Route Manager Import/Export) ----------
    FileDialog {
        id: gpxImportDialog
        title: qsTr("Import GPX")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("GPX files (*.gpx)"), qsTr("All files (*)")]
        onAccepted: {
            const counts = chart.importGpx(selectedFile)
            if (counts.routes === undefined) {
                routeDrawer.showGpxStatus(qsTr("Import failed — not a GPX file?"))
                return
            }
            routeDrawer.showGpxStatus(
                qsTr("Imported %1 routes, %2 tracks, %3 marks (%4 duplicates skipped)")
                    .arg(counts.routes).arg(counts.tracks)
                    .arg(counts.waypoints).arg(counts.duplicates))
        }
    }
    FileDialog {
        id: gpxExportAllDialog
        title: qsTr("Export all as GPX")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "gpx"
        nameFilters: [qsTr("GPX files (*.gpx)")]
        onAccepted: routeDrawer.showGpxStatus(
            chart.exportGpxAll(selectedFile) ? qsTr("Exported all objects")
                                             : qsTr("Export failed"))
    }
    FileDialog {
        id: gpxExportRouteDialog
        title: qsTr("Export route as GPX")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "gpx"
        nameFilters: [qsTr("GPX files (*.gpx)")]
        property int routeIndex: -1
        onAccepted: routeDrawer.showGpxStatus(
            chart.exportGpxRoute(routeIndex, selectedFile)
                ? qsTr("Route exported") : qsTr("Export failed"))
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Routes & marks")
                font.pointSize: 14; font.bold: true
                Layout.fillWidth: true
            }
            ToolButton { text: "✕"; onClicked: routeDrawer.close() }
        }

        TabBar {
            id: drawerTabs
            Layout.fillWidth: true
            TabButton { text: qsTr("Routes") }
            TabButton { text: qsTr("Marks") }
            TabButton { text: qsTr("Tracks") }
        }

        // GPX interchange (P3.19): file-based import / export of nav objects.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Button {
                text: qsTr("Import GPX…")
                onClicked: gpxImportDialog.open()
            }
            Button {
                text: qsTr("Export all…")
                onClicked: gpxExportAllDialog.open()
            }
            Item { Layout.fillWidth: true }
        }
        Label {
            visible: routeDrawer.gpxStatus.length > 0
            text: routeDrawer.gpxStatus
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            font.pointSize: 10
            color: "#3b82f6"
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: drawerTabs.currentIndex

            // --- Routes page ---------------------------------------------
            ColumnLayout {
                spacing: 6
                Label {
                    text: qsTr("Routes (") +
                          (routeDrawer.rl ? routeDrawer.rl.routes.length : 0) + ")"
                    font.pointSize: 11; color: "#9aa0a6"
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: routeDrawer.rl ? routeDrawer.rl.routes : []
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        property bool editing: false
                        // Is this route the one currently being followed?
                        // (P3.16) -- live via the follower's signal.
                        property bool isActiveRoute:
                            chart.routeFollower.activeRouteGuid.length > 0 &&
                            chart.routeFollower.activeRouteGuid === modelData.guid
                        width: ListView.view.width
                        height: tileCol.implicitHeight + 16
                        radius: 6
                        color: tileMouse.containsMouse ? "#26ffffff" : "#14ffffff"
                        border.color: isActiveRoute ? "#ff5a28" : "#33808080"
                        border.width: isActiveRoute ? 2 : 1
                        MouseArea {
                            id: tileMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: chart.showRoute(index)
                        }
                        ColumnLayout {
                            id: tileCol
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                ToolButton {
                                    text: "👁"
                                    font.pointSize: 13
                                    implicitWidth: 34
                                    opacity: (chart.routeVisibilityRevision,
                                              chart.routeVisible(index)) ? 1.0 : 0.3
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Show / hide this route")
                                    onClicked: chart.setRouteVisible(
                                                   index, !chart.routeVisible(index))
                                }
                                Label {
                                    visible: !editing
                                    text: modelData.name.length > 0 ? modelData.name
                                                                    : qsTr("(unnamed)")
                                    font.pointSize: 13; font.bold: true
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                                TextField {
                                    id: nameEdit
                                    visible: editing
                                    Layout.fillWidth: true
                                    selectByMouse: true
                                    font.pointSize: 13
                                    onAccepted: {
                                        chart.renameRoute(index, text)
                                        editing = false
                                    }
                                    Keys.onEscapePressed: editing = false
                                    onActiveFocusChanged:
                                        if (!activeFocus && editing) {
                                            chart.renameRoute(index, text)
                                            editing = false
                                        }
                                }
                                ToolButton {
                                    text: "⋯"
                                    font.pointSize: 15
                                    onClicked: tileMenu.open()
                                    Menu {
                                        id: tileMenu
                                        MenuItem {
                                            text: isActiveRoute ? qsTr("Deactivate")
                                                                : qsTr("Activate")
                                            onTriggered: isActiveRoute
                                                ? chart.deactivateRoute()
                                                : chart.activateRoute(index)
                                        }
                                        MenuItem {
                                            text: qsTr("Skip waypoint")
                                            enabled: isActiveRoute
                                            onTriggered: chart.skipWaypoint()
                                        }
                                        MenuSeparator {}
                                        MenuItem {
                                            text: qsTr("Edit")
                                            onTriggered: chart.editRoute(index)
                                        }
                                        MenuItem {
                                            text: qsTr("Details…")
                                            onTriggered: routeDrawer.routeDetailsRequested(
                                                index, modelData.name)
                                        }
                                        MenuItem {
                                            text: qsTr("Duplicate")
                                            onTriggered: chart.duplicateRoute(index)
                                        }
                                        MenuItem {
                                            text: qsTr("Reverse")
                                            onTriggered: chart.reverseRoute(index)
                                        }
                                        MenuItem {
                                            text: qsTr("Export GPX…")
                                            onTriggered: {
                                                gpxExportRouteDialog.routeIndex = index
                                                gpxExportRouteDialog.open()
                                            }
                                        }
                                        MenuSeparator {}
                                        MenuItem {
                                            text: qsTr("Delete")
                                            onTriggered: drawerConfirmDelete.ask(
                                                qsTr("Delete route \"%1\"?").arg(modelData.name),
                                                function() { chart.deleteRoute(index) })
                                        }
                                    }
                                }
                            }
                            Label {
                                text: modelData.lengthNm.toFixed(1) + qsTr(" NM · ") +
                                      Math.max(0, modelData.points - 1) + qsTr(" legs")
                                color: "#9aa0a6"; font.pointSize: 10
                            }
                        }
                    }
                }
            }

            // --- Marks page ----------------------------------------------
            ColumnLayout {
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Marks (") +
                              (routeDrawer.rl ? routeDrawer.rl.waypoints.length : 0) + ")"
                        font.pointSize: 11; color: "#9aa0a6"
                        Layout.fillWidth: true
                    }
                    Label { text: qsTr("Sort"); font.pointSize: 10; color: "#9aa0a6" }
                    ComboBox {
                        model: [qsTr("Recent"), qsTr("Nearest")]
                        currentIndex: routeDrawer.rl ? routeDrawer.rl.markSortMode : 0
                        onActivated: if (routeDrawer.rl) routeDrawer.rl.markSortMode = currentIndex
                        implicitWidth: 120
                    }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: routeDrawer.rl ? routeDrawer.rl.waypoints : []
                    delegate: Rectangle {
                        required property var modelData
                        property bool editing: false
                        width: ListView.view.width
                        height: mtileCol.implicitHeight + 16
                        radius: 6
                        color: mtileMouse.containsMouse ? "#26ffffff" : "#14ffffff"
                        border.color: "#33808080"; border.width: 1
                        MouseArea {
                            id: mtileMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: chart.showMark(modelData.guid)
                        }
                        ColumnLayout {
                            id: mtileCol
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                ToolButton {
                                    text: "👁"
                                    font.pointSize: 13
                                    implicitWidth: 34
                                    opacity: modelData.visible ? 1.0 : 0.3
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Show / hide this mark")
                                    onClicked: chart.setMarkVisible(
                                                   modelData.guid, !modelData.visible)
                                }
                                Image {
                                    source: "image://wpicon/" + modelData.icon
                                    Layout.preferredWidth: 24
                                    Layout.preferredHeight: 22
                                    sourceSize.height: 22
                                    fillMode: Image.PreserveAspectFit
                                }
                                Label {
                                    visible: !editing
                                    text: modelData.name
                                    font.pointSize: 13; font.bold: true
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                                TextField {
                                    id: mNameEdit
                                    visible: editing
                                    Layout.fillWidth: true
                                    selectByMouse: true
                                    font.pointSize: 13
                                    onAccepted: {
                                        chart.renameMark(modelData.guid, text)
                                        editing = false
                                    }
                                    Keys.onEscapePressed: editing = false
                                    onActiveFocusChanged:
                                        if (!activeFocus && editing) {
                                            chart.renameMark(modelData.guid, text)
                                            editing = false
                                        }
                                }
                                ToolButton {
                                    text: "⋯"
                                    font.pointSize: 15
                                    onClicked: mtileMenu.open()
                                    Menu {
                                        id: mtileMenu
                                        MenuItem {
                                            text: qsTr("Edit")
                                            onTriggered: routeDrawer.editMarkRequested(
                                                modelData.guid, modelData.name,
                                                modelData.comment, modelData.icon)
                                        }
                                        MenuItem {
                                            text: qsTr("Rename")
                                            onTriggered: {
                                                mNameEdit.text = modelData.name
                                                editing = true
                                                mNameEdit.forceActiveFocus()
                                                mNameEdit.selectAll()
                                            }
                                        }
                                        MenuSeparator {}
                                        MenuItem {
                                            text: qsTr("Delete")
                                            onTriggered: drawerConfirmDelete.ask(
                                                qsTr("Delete mark \"%1\"?").arg(modelData.name),
                                                function() { chart.deleteMark(modelData.guid) })
                                        }
                                    }
                                }
                            }
                            Label {
                                visible: modelData.comment.length > 0
                                text: modelData.comment
                                color: "#9aa0a6"; font.pointSize: 10
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2; elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: modelData.rangeNm >= 0
                                      ? modelData.rangeNm.toFixed(1) + qsTr(" NM away")
                                      : qsTr("position unknown")
                                color: "#9aa0a6"; font.pointSize: 9
                            }
                        }
                    }
                }
            }

            // --- Tracks page ---------------------------------------------
            ColumnLayout {
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: chart.trackRecording ? qsTr("Stop") : qsTr("Start")
                        onClicked: chart.trackRecording = !chart.trackRecording
                    }
                    Button {
                        text: qsTr("Reset")
                        enabled: chart.trackRecording
                        onClicked: chart.resetTrack()
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: qsTr("(") +
                              (routeDrawer.rl ? routeDrawer.rl.tracks.length : 0) + ")"
                        font.pointSize: 11; color: "#9aa0a6"
                    }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: routeDrawer.rl ? routeDrawer.rl.tracks : []
                    delegate: Rectangle {
                        required property var modelData
                        property bool editing: false
                        width: ListView.view.width
                        height: ttileCol.implicitHeight + 16
                        radius: 6
                        color: ttileMouse.containsMouse ? "#26ffffff" : "#14ffffff"
                        border.color: modelData.active ? "#7f9b00c8" : "#33808080"
                        border.width: modelData.active ? 2 : 1
                        MouseArea {
                            id: ttileMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: chart.showTrack(modelData.guid)
                        }
                        ColumnLayout {
                            id: ttileCol
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                ToolButton {
                                    text: "👁"
                                    font.pointSize: 13
                                    implicitWidth: 34
                                    opacity: modelData.visible ? 1.0 : 0.3
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Show / hide this track")
                                    onClicked: chart.setTrackVisible(
                                                   modelData.guid, !modelData.visible)
                                }
                                Label {
                                    visible: !editing
                                    text: modelData.name +
                                          (modelData.active ? qsTr("  ● REC") : "")
                                    font.pointSize: 13; font.bold: true
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    color: modelData.active ? "#d070ff"
                                                            : palette.windowText
                                }
                                TextField {
                                    id: tNameEdit
                                    visible: editing
                                    Layout.fillWidth: true
                                    selectByMouse: true
                                    font.pointSize: 13
                                    onAccepted: {
                                        chart.renameTrack(modelData.guid, text)
                                        editing = false
                                    }
                                    Keys.onEscapePressed: editing = false
                                    onActiveFocusChanged:
                                        if (!activeFocus && editing) {
                                            chart.renameTrack(modelData.guid, text)
                                            editing = false
                                        }
                                }
                                ToolButton {
                                    text: "⋯"
                                    font.pointSize: 15
                                    onClicked: ttileMenu.open()
                                    Menu {
                                        id: ttileMenu
                                        MenuItem {
                                            text: qsTr("Rename")
                                            onTriggered: {
                                                tNameEdit.text = modelData.name
                                                editing = true
                                                tNameEdit.forceActiveFocus()
                                                tNameEdit.selectAll()
                                            }
                                        }
                                        MenuSeparator {}
                                        MenuItem {
                                            text: qsTr("Delete")
                                            onTriggered: drawerConfirmDelete.ask(
                                                qsTr("Delete track \"%1\"?").arg(modelData.name),
                                                function() { chart.deleteTrack(modelData.guid) })
                                        }
                                    }
                                }
                            }
                            Label {
                                text: modelData.lengthNm.toFixed(1) + qsTr(" NM")
                                color: "#9aa0a6"; font.pointSize: 10
                            }
                        }
                    }
                }
            }
        }
    }
}
