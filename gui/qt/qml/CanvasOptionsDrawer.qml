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

// --- Canvas options: slide-out display panel from the right (mirrors
//     OpenCPN's MUIBar CanvasOptions). Native right-edge Drawer.
//     The shared "Vector chart detail" checklist lives in
//     VectorDetailList.qml (P3.17) -- used by BOTH this drawer and the
//     Options > Charts > Vector Display tab, so the two can never drift.
Drawer {
    id: canvasOptions

    // The application shell window (drawer sizing tracks it).
    required property var appWindow

    edge: Qt.RightEdge
    width: Math.min(320, appWindow.width * 0.85)
    height: appWindow.height

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Label { text: qsTr("Chart display category"); font.bold: true }
        ButtonGroup { id: catGroup }
        Repeater {
            model: [ { label: qsTr("Base"), cat: 0 },
                     { label: qsTr("Standard"), cat: 1 },
                     { label: qsTr("All"), cat: 2 },
                     { label: qsTr("Mariner's standard"), cat: 3 } ]
            delegate: RadioButton {
                required property var modelData
                text: modelData.label
                ButtonGroup.group: catGroup
                checked: chart.displayCategory === modelData.cat
                onClicked: chart.displayCategory = modelData.cat
            }
        }

        MenuSeparator { Layout.fillWidth: true }

        // Same vector-detail checklist as Options > Charts > Vector
        // Display, via the shared VectorDetailList component (kept in sync).
        VectorDetailList { Layout.fillWidth: true }

        MenuSeparator { Layout.fillWidth: true }

        // Quick display: the common view controls, shared with the
        // Options > Display page via the `display` backend.
        Label { text: qsTr("Quick display"); font.bold: true }
        CheckBox {
            text: qsTr("Follow own ship")
            checked: chart.followOwnShip
            onToggled: chart.followOwnShip = checked
        }
        CheckBox {
            text: qsTr("Compass window")
            checked: DisplayConfig.showCompass
            onToggled: DisplayConfig.showCompass = checked
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Orientation:") }
            RadioButton {
                text: qsTr("N-Up")
                checked: DisplayConfig.navMode === 0
                onClicked: DisplayConfig.navMode = 0
            }
            RadioButton {
                text: qsTr("C-Up")
                checked: DisplayConfig.navMode === 1
                onClicked: DisplayConfig.navMode = 1
            }
            RadioButton {
                text: qsTr("H-Up")
                checked: DisplayConfig.navMode === 2
                onClicked: DisplayConfig.navMode = 2
            }
        }
        Item { Layout.fillHeight: true }
    }
}
