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

// --- Data Monitor: scrolling view of decoded NMEA/N2K messages, to
//     diagnose what a connection is actually delivering.
Window {
    id: dataMonitorWindow
    title: qsTr("Data monitor")
    flags: Qt.Dialog
    width: 580
    height: 420
    color: palette.window

    // Source filter ("" = all). Lets you isolate one connection to see if
    // it is delivering -- e.g. select the N2000 source to check it.
    property string srcFilter: ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            CheckBox {
                text: qsTr("Pause")
                checked: chart.nmeaMonitor.paused
                onToggled: chart.nmeaMonitor.paused = checked
            }
            Label { text: qsTr("Source:") }
            ComboBox {
                id: srcCombo
                Layout.preferredWidth: 240
                model: [qsTr("All")].concat(chart.nmeaMonitor.sources)
                onActivated: dataMonitorWindow.srcFilter =
                             (currentIndex === 0 ? "" : currentText)
            }
            Item { Layout.fillWidth: true }
            Label {
                text: monModel.count + qsTr(" lines")
                color: palette.placeholderText
            }
            Button {
                text: qsTr("Clear")
                onClicked: monModel.clear()
            }
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 2
            ListView {
                id: monView
                anchors.fill: parent
                clip: true
                model: ListModel { id: monModel }
                delegate: Text {
                    required property string line
                    required property string src
                    readonly property bool shown:
                        dataMonitorWindow.srcFilter === "" ||
                        dataMonitorWindow.srcFilter === src
                    width: monView.width
                    height: shown ? implicitHeight : 0
                    visible: shown
                    text: line
                    font.family: "monospace"
                    font.pointSize: 11
                    elide: Text.ElideRight
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }
    }

    Connections {
        target: chart.nmeaMonitor
        function onLineReceived(line, source) {
            monModel.append({ "line": line, "src": source })
            if (monModel.count > 1000) monModel.remove(0)
            monView.positionViewAtEnd()  // auto-scroll
        }
    }
}
