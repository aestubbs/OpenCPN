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

// --- Mark editor (P3.7): one dialog for "New mark" (dropped via the chart
//     right-click) and "Edit mark" (the drawer tile's Edit). Captures name,
//     comment, and a visual icon pick (images via the wpicon provider).
Dialog {
    id: markEditor
    title: editMode ? qsTr("Edit mark") : qsTr("New mark")
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 380
    standardButtons: Dialog.Ok | Dialog.Cancel

    property bool editMode: false
    property string guid: ""
    property string iconName: "triangle"

    function openNew() {
        editMode = false; guid = "";
        markNameField.text = ""; markCommentField.text = "";
        // Default to the configured mark icon (Options > User Interface >
        // Routes & Marks).
        iconName = RouteDefaultsConfig.waypointIcon || "triangle";
        open()
    }
    function openForEdit(g, nm, cm, ic) {
        editMode = true; guid = g;
        markNameField.text = nm; markCommentField.text = cm;
        iconName = ic.length > 0 ? ic : "triangle";
        open()
    }
    onAccepted: {
        if (editMode) {
            chart.renameMark(guid, markNameField.text)
            chart.setMarkComment(guid, markCommentField.text)
            chart.setMarkIcon(guid, iconName)
        } else {
            chart.dropMarkHere(markNameField.text, markCommentField.text,
                               iconName)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        Label {
            visible: !markEditor.editMode
            text: qsTr("At ") + chart.markDropLat().toFixed(4) + ", " +
                  chart.markDropLon().toFixed(4)
            color: "#9aa0a6"; font.pointSize: 10
        }
        TextField {
            id: markNameField
            placeholderText: qsTr("Name")
            Layout.fillWidth: true
            selectByMouse: true
        }
        TextField {
            id: markCommentField
            placeholderText: qsTr("Comment")
            Layout.fillWidth: true
            selectByMouse: true
        }
        Label { text: qsTr("Icon"); font.pointSize: 10; color: "#9aa0a6" }
        GridView {
            Layout.fillWidth: true
            Layout.preferredHeight: 132
            clip: true
            cellWidth: 44; cellHeight: 44
            model: chart.markIconNames()
            delegate: Rectangle {
                required property var modelData
                width: 42; height: 42; radius: 4
                color: modelData === markEditor.iconName ? "#553b82f6"
                                                         : "transparent"
                border.color: modelData === markEditor.iconName ? "#3b82f6"
                                                                : "#33808080"
                Image {
                    anchors.centerIn: parent
                    source: "image://wpicon/" + modelData
                    sourceSize.height: 28
                    fillMode: Image.PreserveAspectFit
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: markEditor.iconName = modelData
                }
            }
        }
    }
}
