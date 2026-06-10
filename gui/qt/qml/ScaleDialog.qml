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

// Scale entry: clicking the status-bar scale opens this to type a 1:N
// value. Free-form like wx (mui_bar.cpp OnScaleSelected); ChartCanvas
// clamps to 1:1,000 .. 1:3,000,000 (no snapping to standard scales).
Dialog {
    id: scaleDialog
    title: qsTr("Set chart scale")
    modal: true
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.Ok | Dialog.Cancel

    // Open pre-filled with the current scale denominator, ready to overtype.
    function openWithScale(denominator) {
        scaleEntry.text = denominator
        open()
        scaleEntry.forceActiveFocus()
        scaleEntry.selectAll()
    }

    onAccepted: {
        const n = parseInt(scaleEntry.text, 10)
        if (!isNaN(n) && n > 0) chart.setScaleDenominator(n)
    }
    RowLayout {
        Label { text: "1:" }
        TextField {
            id: scaleEntry
            Layout.preferredWidth: 140
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 1000; top: 3000000 }
            selectByMouse: true
            onAccepted: scaleDialog.accept()
        }
    }
}
