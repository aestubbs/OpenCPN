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
import QtQuick.Controls
import QtQuick.Layouts

// --- Route details: the route name + a per-route mark icon for its points
//     (the dot is the default; pick an icon to override all the route's
//     points). Opened from the routes drawer ⋯ menu.
Window {
    id: routeDetailsDialog
    title: qsTr("Route details")
    flags: Qt.Dialog
    modality: Qt.ApplicationModal
    width: 420
    height: 360
    color: palette.window

    property int routeIndex: -1
    property string iconName: ""

    // Build the icon model by hand: "" (plain dot) then the catalogue.
    // [].concat(aQStringList) appends the list as a single element rather
    // than spreading it, which broke the grid.
    function iconModel() {
        var names = chart.markIconNames()
        var out = [""]
        for (var i = 0; i < names.length; ++i) out.push(names[i])
        return out
    }
    function openFor(idx, nm) {
        routeIndex = idx
        routeNameField.text = nm
        iconName = chart.routePointIcon(idx)
        show(); raise(); requestActivate()
        routeNameField.forceActiveFocus()
    }
    function apply() {
        if (routeIndex < 0) return
        chart.renameRoute(routeIndex, routeNameField.text)
        chart.setRoutePointIcon(routeIndex, iconName)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 10
            Layout.fillWidth: true

            Label { text: qsTr("Name:"); Layout.alignment: Qt.AlignRight }
            TextField {
                id: routeNameField
                Layout.fillWidth: true
                selectByMouse: true
            }

            Label {
                text: qsTr("Point icon:")
                Layout.alignment: Qt.AlignRight | Qt.AlignTop
                Layout.topMargin: 6
            }
            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 4
                GridView {
                    id: iconGrid
                    anchors.fill: parent
                    clip: true
                    cellWidth: 46; cellHeight: 46
                    model: routeDetailsDialog.iconModel()
                    delegate: ItemDelegate {
                        required property var modelData
                        width: 44; height: 44
                        padding: 0
                        highlighted: modelData === routeDetailsDialog.iconName
                        onClicked: routeDetailsDialog.iconName = modelData
                        ToolTip.visible: hovered && modelData.length > 0
                        ToolTip.text: modelData
                        contentItem: Item {
                            Image {
                                anchors.centerIn: parent
                                visible: modelData.length > 0
                                source: modelData.length > 0
                                        ? "image://wpicon/" + modelData : ""
                                sourceSize.height: 30
                                fillMode: Image.PreserveAspectFit
                            }
                            // Empty entry = the plain "dot" default.
                            Rectangle {
                                visible: modelData.length === 0
                                anchors.centerIn: parent
                                width: 9; height: 9; radius: 4.5
                                color: palette.windowText
                            }
                        }
                    }
                }
            }
        }

        DialogButtonBox {
            Layout.fillWidth: true
            standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
            onAccepted: { routeDetailsDialog.apply(); routeDetailsDialog.close() }
            onRejected: routeDetailsDialog.close()
        }
    }
}
