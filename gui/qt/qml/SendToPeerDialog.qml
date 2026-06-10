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

// Send to Peer (P3.18 tier 4, wx SendToPeerDlg): pick a discovered OpenCPN
// instance (mDNS) or type host[:port], then transfer the route / mark /
// track via the model's REST client. The peer may demand its pairing PIN
// (shown on ITS screen) -- a nested prompt collects it. Native dialog
// window per the app convention.
Window {
    id: sendToPeer
    title: qsTr("Send to peer")
    flags: Qt.Dialog
    width: 420
    height: 400
    color: palette.window

    // What to send: exactly one of these is set by the open* functions.
    property int routeIndex: -1
    property string markGuid: ""
    property string trackGuid: ""
    property string objectLabel: ""

    readonly property var ps: chart.peerSend

    function openForRoute(idx, name) {
        routeIndex = idx; markGuid = ""; trackGuid = ""
        objectLabel = qsTr("Route \"%1\"").arg(name)
        openCommon()
    }
    function openForMark(guid, name) {
        routeIndex = -1; markGuid = guid; trackGuid = ""
        objectLabel = qsTr("Mark \"%1\"").arg(name)
        openCommon()
    }
    function openForTrack(guid, name) {
        routeIndex = -1; markGuid = ""; trackGuid = guid
        objectLabel = qsTr("Track \"%1\"").arg(name)
        openCommon()
    }
    function openCommon() {
        show(); raise(); requestActivate()
        if (ps.peers.length === 0) ps.scan()
    }
    function destIp() {
        if (peerList.currentIndex >= 0 && peerList.currentIndex < ps.peers.length) {
            const p = ps.peers[peerList.currentIndex]
            return p.port.length > 0 ? p.ip + ":" + p.port : p.ip
        }
        return manualIp.text.trim()
    }
    function doSend() {
        const ip = destIp()
        if (ip.length === 0) return
        if (routeIndex >= 0) ps.sendRoute(routeIndex, ip, activateBox.checked)
        else if (markGuid.length > 0) ps.sendMark(markGuid, ip)
        else if (trackGuid.length > 0) ps.sendTrack(trackGuid, ip)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 10

        Label { text: sendToPeer.objectLabel; font.bold: true }

        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Discovered OpenCPN instances"); Layout.fillWidth: true }
            Button {
                text: sendToPeer.ps.scanning ? qsTr("Scanning…") : qsTr("Rescan")
                enabled: !sendToPeer.ps.scanning
                onClicked: sendToPeer.ps.scan()
            }
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 2
            ListView {
                id: peerList
                anchors.fill: parent
                clip: true
                model: sendToPeer.ps.peers
                currentIndex: -1
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: peerList.width
                    highlighted: peerList.currentIndex === index
                    onClicked: peerList.currentIndex =
                                   (peerList.currentIndex === index ? -1 : index)
                    contentItem: Label {
                        text: modelData.name + "  (" + modelData.ip +
                              (modelData.port.length ? ":" + modelData.port : "") + ")"
                        elide: Text.ElideRight
                    }
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Or host[:port]:") }
            TextField {
                id: manualIp
                Layout.fillWidth: true
                placeholderText: qsTr("192.168.1.5:8443")
                selectByMouse: true
                onTextEdited: peerList.currentIndex = -1
            }
        }
        CheckBox {
            id: activateBox
            visible: sendToPeer.routeIndex >= 0
            text: qsTr("Activate the route on the peer after transfer")
        }
        Label {
            visible: sendToPeer.ps.status.length > 0
            text: sendToPeer.ps.status
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            color: "#3b82f6"
        }
        DialogButtonBox {
            Layout.fillWidth: true
            Button {
                text: sendToPeer.ps.sending ? qsTr("Sending…") : qsTr("Send")
                enabled: !sendToPeer.ps.sending && sendToPeer.destIp().length > 0
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            standardButtons: DialogButtonBox.Close
            onAccepted: sendToPeer.doSend()
            onRejected: sendToPeer.close()
        }
    }

    // PIN prompt: the peer displays a pairing PIN on ITS screen; the model
    // transfer blocks until we answer.
    Window {
        id: pinPrompt
        title: qsTr("Peer pairing PIN")
        flags: Qt.Dialog
        modality: Qt.ApplicationModal
        width: 320
        height: 150
        color: palette.window
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10
            Label {
                text: qsTr("Type the PIN shown on the peer's screen:")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            TextField {
                id: pinField
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhDigitsOnly
                selectByMouse: true
                onAccepted: { sendToPeer.ps.providePin(text); pinPrompt.close() }
            }
            DialogButtonBox {
                Layout.fillWidth: true
                standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
                onAccepted: { sendToPeer.ps.providePin(pinField.text); pinPrompt.close() }
                onRejected: { sendToPeer.ps.cancelPin(); pinPrompt.close() }
            }
        }
    }
    Connections {
        target: sendToPeer.ps
        function onPinRequested() {
            pinField.text = ""
            pinPrompt.show(); pinPrompt.raise(); pinPrompt.requestActivate()
            pinField.forceActiveFocus()
        }
    }
}
