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

// Delete-confirmation dialog (P3.6, wx g_bConfirmObjectDelete): call
// ask(message, fn) and fn runs on OK -- or immediately, with no dialog,
// when the user has turned confirmation off in Options > Routes & Marks.
// A real dialog WINDOW (native controls, app-modal) -- the app-wide
// dialog convention (user feedback 2026-06-10).
Window {
    id: confirmDialog
    title: qsTr("Confirm delete")
    flags: Qt.Dialog
    modality: Qt.ApplicationModal
    width: 360
    height: 130
    color: palette.window

    property string message: ""
    property var action: null

    function ask(msg, fn) {
        if (!RouteDefaultsConfig.confirmObjectDelete) {
            fn()
            return
        }
        message = msg
        action = fn
        show(); raise(); requestActivate()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 10
        Label {
            text: confirmDialog.message
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        DialogButtonBox {
            Layout.fillWidth: true
            standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
            onAccepted: {
                confirmDialog.close()
                if (confirmDialog.action) confirmDialog.action()
            }
            onRejected: confirmDialog.close()
        }
    }
}
