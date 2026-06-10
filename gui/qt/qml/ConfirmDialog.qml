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

// Delete-confirmation dialog (P3.6, wx g_bConfirmObjectDelete): call
// ask(message, fn) and fn runs on OK -- or immediately, with no dialog,
// when the user has turned confirmation off in Options > Routes & Marks.
Dialog {
    id: confirmDialog
    title: qsTr("Confirm delete")
    modal: true
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.Ok | Dialog.Cancel

    property string message: ""
    property var action: null

    function ask(msg, fn) {
        if (!RouteDefaultsConfig.confirmObjectDelete) {
            fn()
            return
        }
        message = msg
        action = fn
        open()
    }
    onAccepted: if (action) action()

    Label {
        text: confirmDialog.message
        wrapMode: Text.Wrap
    }
}
