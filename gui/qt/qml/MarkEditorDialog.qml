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
import opencpn.qt

// --- Mark editor (P3.7): one dialog for "New mark" (dropped via the chart
//     right-click) and "Edit mark" (the drawer tile's Edit). Captures name,
//     comment, and a visual icon pick (images via the wpicon provider).
//     A real dialog WINDOW (native title bar + controls, app-modal), not an
//     in-scene sheet -- the app-wide dialog convention (cf.
//     RouteDetailsWindow; user feedback 2026-06-10).
Window {
    id: markEditor
    title: editMode ? qsTr("Edit mark") : qsTr("New mark")
    flags: Qt.Dialog
    modality: Qt.ApplicationModal
    width: 420
    height: 560
    color: palette.window

    property bool editMode: false
    property string guid: ""
    property string iconName: "triangle"

    function openNew() {
        editMode = false; guid = "";
        markNameField.text = ""; markCommentField.text = "";
        // Default to the configured mark icon (Options > User Interface >
        // Routes & Marks).
        iconName = RouteDefaultsConfig.waypointIcon || "triangle";
        show(); raise(); requestActivate()
        markNameField.forceActiveFocus()
    }
    function openForEdit(g, nm, cm, ic) {
        editMode = true; guid = g;
        markNameField.text = nm; markCommentField.text = cm;
        iconName = ic.length > 0 ? ic : "triangle";
        // Per-mark rings + SCAMIN (P3.6): load the current values by guid.
        ringsCheck.checked = false
        ringsCount.value = 0
        ringsStep.text = "1.0"
        ringsUnits.currentIndex = 0
        scaminField.text = "0"
        const wps = chart.routeList.waypoints
        for (let i = 0; i < wps.length; ++i) {
            if (wps[i].guid === g) {
                ringsCheck.checked = wps[i].showRings
                ringsCount.value = wps[i].ringCount
                ringsStep.text = Number(wps[i].ringStep).toFixed(1)
                ringsUnits.currentIndex = wps[i].ringUnits
                scaminField.text = String(wps[i].scamin)
                break
            }
        }
        show(); raise(); requestActivate()
        markNameField.forceActiveFocus()
    }
    function apply() {
        if (editMode) {
            chart.renameMark(guid, markNameField.text)
            chart.setMarkComment(guid, markCommentField.text)
            chart.setMarkIcon(guid, iconName)
            chart.setMarkRangeRings(guid, ringsCheck.checked,
                                    ringsCount.value,
                                    parseFloat(ringsStep.text) || 0,
                                    ringsUnits.currentIndex)
            chart.setMarkScamin(guid, parseInt(scaminField.text) || 0)
        } else {
            chart.dropMarkHere(markNameField.text, markCommentField.text,
                               iconName)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 10

        Label {
            visible: !markEditor.editMode
            text: qsTr("At ") + chart.markDropLat().toFixed(4) + ", " +
                  chart.markDropLon().toFixed(4)
            color: palette.placeholderText; font.pointSize: 10
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
        Label {
            text: qsTr("Icon")
            font.pointSize: 10; color: palette.placeholderText
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 4
            GridView {
                anchors.fill: parent
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
                ScrollBar.vertical: ScrollBar {}
            }
        }
        // Per-mark range rings + SCAMIN (P3.6) -- edit mode only (a new mark
        // can be edited right after dropping it).
        RowLayout {
            visible: markEditor.editMode
            spacing: 8
            CheckBox { id: ringsCheck; text: qsTr("Range rings") }
            SpinBox { id: ringsCount; from: 0; to: 10; enabled: ringsCheck.checked }
            TextField {
                id: ringsStep
                Layout.preferredWidth: 60
                enabled: ringsCheck.checked
                validator: DoubleValidator { bottom: 0.1; top: 100 }
                selectByMouse: true
            }
            ComboBox {
                id: ringsUnits
                Layout.preferredWidth: 80
                enabled: ringsCheck.checked
                model: [qsTr("NM"), qsTr("km")]
            }
        }
        RowLayout {
            visible: markEditor.editMode
            spacing: 8
            Label { text: qsTr("Hide beyond 1:") }
            TextField {
                id: scaminField
                Layout.preferredWidth: 110
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 0; top: 100000000 }
                selectByMouse: true
            }
            Label {
                text: qsTr("(SCAMIN; 0 = always show)")
                color: palette.placeholderText; font.pointSize: 10
            }
        }
        DialogButtonBox {
            Layout.fillWidth: true
            standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
            onAccepted: { markEditor.apply(); markEditor.close() }
            onRejected: markEditor.close()
        }
    }
}
