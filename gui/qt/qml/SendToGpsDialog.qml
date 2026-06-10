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

// Send to GPS (P3.18 tier 4, wx SendToGpsDlg): upload a route or mark to a
// chartplotter / handheld over a serial port (NMEA-0183 WPL/RTE). Native
// dialog window per the app convention.
Window {
    id: sendToGps
    title: qsTr("Send to GPS")
    flags: Qt.Dialog
    width: 430
    height: 270
    color: palette.window

    property int routeIndex: -1
    property string markGuid: ""
    property string objectLabel: ""
    property var ports: []

    readonly property var up: chart.gpsUpload

    function openForRoute(idx, name) {
        routeIndex = idx; markGuid = ""
        objectLabel = qsTr("Route \"%1\"").arg(name)
        openCommon()
    }
    function openForMark(guid, name) {
        routeIndex = -1; markGuid = guid
        objectLabel = qsTr("Mark \"%1\"").arg(name)
        openCommon()
    }
    function openCommon() {
        ports = chart.connections.availableSerialPorts()
        show(); raise(); requestActivate()
    }
    function doSend() {
        if (portBox.currentIndex < 0 || portBox.currentIndex >= ports.length)
            return
        const port = ports[portBox.currentIndex].port
        if (routeIndex >= 0)
            up.sendRoute(routeIndex, port, waypointsBox.checked)
        else if (markGuid.length > 0)
            up.sendMark(markGuid, port)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 10

        Label { text: sendToGps.objectLabel; font.bold: true }

        GridLayout {
            columns: 2
            columnSpacing: 10; rowSpacing: 8
            Layout.fillWidth: true
            Label { text: qsTr("Serial port:"); Layout.alignment: Qt.AlignRight }
            ComboBox {
                id: portBox
                Layout.fillWidth: true
                model: sendToGps.ports
                textRole: "description"
                displayText: sendToGps.ports.length === 0
                             ? qsTr("(no serial ports found)") : currentText
            }
        }
        CheckBox {
            id: waypointsBox
            visible: sendToGps.routeIndex >= 0
            checked: true
            text: qsTr("Send the route's waypoints (WPL) with it")
        }
        ProgressBar {
            Layout.fillWidth: true
            visible: sendToGps.up.sending
            from: 0; to: 100
            value: sendToGps.up.progress
        }
        Label {
            visible: sendToGps.up.status.length > 0
            text: sendToGps.up.status
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            color: "#3b82f6"
        }
        Item { Layout.fillHeight: true }
        DialogButtonBox {
            Layout.fillWidth: true
            Button {
                text: sendToGps.up.sending ? qsTr("Sending…") : qsTr("Send")
                enabled: !sendToGps.up.sending && sendToGps.ports.length > 0
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            standardButtons: DialogButtonBox.Close
            onAccepted: sendToGps.doSend()
            onRejected: sendToGps.close()
        }
    }
}
