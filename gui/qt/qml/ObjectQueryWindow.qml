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

// --- Object query: a real dialog window (P3.9), opened from the chart
//     right-click "Object query here" menu item -- the wx S57Query flow.
//     Binds to ChartCanvas.objectQuery; steps through stacked features.
Window {
    id: objectQueryWindow
    title: qsTr("Object query")
    flags: Qt.Dialog
    width: 420
    height: 480
    color: palette.window

    readonly property var q: chart.objectQuery

    onVisibleChanged: if (!visible) chart.objectQuery.clear()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6

        Label {
            text: objectQueryWindow.q ? objectQueryWindow.q.className : ""
            font.pointSize: 15; font.bold: true
        }
        // Step through the stacked objects (specific -> containing).
        RowLayout {
            Layout.fillWidth: true
            visible: objectQueryWindow.q && objectQueryWindow.q.count > 1
            ToolButton {
                text: "‹"; font.pointSize: 15
                enabled: objectQueryWindow.q && objectQueryWindow.q.index > 0
                onClicked: chart.objectQuery.prev()
            }
            Label {
                text: objectQueryWindow.q
                      ? (objectQueryWindow.q.index + 1) + " / " + objectQueryWindow.q.count
                      : ""
                font.pointSize: 11
            }
            ToolButton {
                text: "›"; font.pointSize: 15
                enabled: objectQueryWindow.q &&
                         objectQueryWindow.q.index < objectQueryWindow.q.count - 1
                onClicked: chart.objectQuery.next()
            }
            Item { Layout.fillWidth: true }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            TextArea {
                readOnly: true
                wrapMode: TextEdit.Wrap
                font.pointSize: 11
                font.family: "monospace"
                text: objectQueryWindow.q ? objectQueryWindow.q.text : ""
            }
        }
        DialogButtonBox {
            Layout.fillWidth: true
            standardButtons: DialogButtonBox.Close
            onRejected: objectQueryWindow.close()
        }
    }
}
