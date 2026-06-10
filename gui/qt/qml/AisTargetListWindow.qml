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

// AIS target list (P3.18 tier 2, wx AISTargetListDialog): the live targets
// sorted nearest-first, with range / bearing / SOG / COG / CPA / TCPA. A row
// click selects the target (info popup); the buttons centre the view on it.
// Snapshot-driven: refreshed via chart.aisTargetSnapshot() on a 2 s timer
// while the window is visible (the list is small; no model machinery needed).
Window {
    id: aisListWindow
    title: qsTr("AIS targets")
    flags: Qt.Dialog
    width: 620
    height: 420
    color: palette.window

    property var targets: []
    property int selectedMmsi: 0

    function refresh() { targets = chart.aisTargetSnapshot() }
    onVisibleChanged: if (visible) refresh()
    Timer {
        interval: 2000; repeat: true
        running: aisListWindow.visible
        onTriggered: aisListWindow.refresh()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        // Header row, matching the grid below.
        component HCell: Label {
            font.bold: true
            font.pointSize: 10
            color: palette.placeholderText
        }
        // One table cell; colour is set per use (the row's danger accent).
        component Cell: Label {
            font.pointSize: 11
            elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: 14
            spacing: 8
            HCell { text: qsTr("Name");  Layout.preferredWidth: 150 }
            HCell { text: qsTr("MMSI");  Layout.preferredWidth: 80 }
            HCell { text: qsTr("Range"); Layout.preferredWidth: 70 }
            HCell { text: qsTr("Brg");   Layout.preferredWidth: 50 }
            HCell { text: qsTr("SOG");   Layout.preferredWidth: 50 }
            HCell { text: qsTr("COG");   Layout.preferredWidth: 50 }
            HCell { text: qsTr("CPA");   Layout.preferredWidth: 70 }
            HCell { text: qsTr("TCPA");  Layout.fillWidth: true }
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 2
            ListView {
                id: listView
                anchors.fill: parent
                clip: true
                model: aisListWindow.targets
                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    width: listView.width
                    height: 30
                    highlighted: modelData.mmsi === aisListWindow.selectedMmsi
                    // Danger targets read in the alert red; SART in its
                    // distress accent.
                    readonly property color rowColor:
                        modelData.isSart ? "#ff5050"
                        : modelData.dangerous ? "#e05060"
                        : palette.text
                    onClicked: {
                        aisListWindow.selectedMmsi = modelData.mmsi
                        chart.selectAisTarget(modelData.mmsi)
                    }
                    onDoubleClicked: chart.centerOnAis(modelData.mmsi)
                    contentItem: RowLayout {
                        spacing: 8
                        Cell { text: row.modelData.name;        color: row.rowColor; Layout.preferredWidth: 150 }
                        Cell { text: row.modelData.mmsi;        color: row.rowColor; Layout.preferredWidth: 80 }
                        Cell { text: row.modelData.rangeText;   color: row.rowColor; Layout.preferredWidth: 70 }
                        Cell { text: row.modelData.bearingText; color: row.rowColor; Layout.preferredWidth: 50 }
                        Cell { text: row.modelData.sogText;     color: row.rowColor; Layout.preferredWidth: 50 }
                        Cell { text: row.modelData.cogText;     color: row.rowColor; Layout.preferredWidth: 50 }
                        Cell { text: row.modelData.cpaText;     color: row.rowColor; Layout.preferredWidth: 70 }
                        Cell { text: row.modelData.tcpaText;    color: row.rowColor; Layout.fillWidth: true }
                    }
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: aisListWindow.targets.length + qsTr(" targets")
                color: palette.placeholderText
                font.pointSize: 10
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Center view")
                enabled: aisListWindow.selectedMmsi !== 0
                onClicked: chart.centerOnAis(aisListWindow.selectedMmsi)
            }
            Button {
                text: qsTr("Close")
                onClicked: aisListWindow.close()
            }
        }
    }
}
