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

// --- About: mirrors the wx About dialog's content, branded OpenCPN-NG. --
Window {
    id: aboutWindow
    title: qsTr("About OpenCPN-NG")
    flags: Qt.Dialog
    width: 480
    height: 380
    color: palette.window

    // Hyperlink-style label (mirrors the wx About's hyperlinks).
    component Link: Label {
        property string url
        color: "#3478f6"
        font.underline: true
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: Qt.openUrlExternally(parent.url) }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 6

        Label {
            text: "OpenCPN-NG"
            font.pointSize: 24; font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: qsTr("The Open Source Chart Plotter")
            opacity: 0.8
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: qsTr("Version %1  ·  Qt edition").arg(appVersion)
            opacity: 0.9
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "© 2000–2026 David S. Register and the OpenCPN Authors"
            opacity: 0.7; font.pointSize: 10
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: qsTr("OpenCPN is a Free Software project, built by sailors.\n" +
                       "This edition renders S-57 / S-52 vector charts through a " +
                       "Qt Quick scene graph.")
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Layout.topMargin: 6
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            spacing: 20
            Link { text: qsTr("Website"); url: "https://opencpn.org" }
            Link { text: qsTr("GitHub"); url: "https://github.com/OpenCPN/OpenCPN" }
            Link { text: qsTr("Donate")
                   url: "https://sourceforge.net/donate/index.php?group_id=180842" }
            Link { text: qsTr("License")
                   url: "https://www.gnu.org/licenses/old-licenses/gpl-2.0.html" }
        }
        Label {
            text: qsTr("Running on Qt ") + qtRuntimeVersion
            opacity: 0.6; font.pointSize: 9
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
        }
        Item { Layout.fillHeight: true }
        DialogButtonBox {
            Layout.fillWidth: true
            standardButtons: DialogButtonBox.Close
            onRejected: aboutWindow.close()
        }
    }
}
