import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The o-charts pane (P4.7) -- fully plugin-provided, wx o-charts_pi
// parity: daemon status, labeled account login, system registration,
// purchased chart sets with one-click install.
ColumnLayout {
    property var pluginContext: null
    spacing: 8

    Label { text: qsTr("o-charts (encrypted)"); font.bold: true }
    Label {
        text: pluginContext && pluginContext.daemonAvailable
              ? qsTr("Decryption helper found: ") + pluginContext.daemonVersion
              : qsTr("oexserverd decryption helper not found — charts cannot be decrypted on this machine.")
        wrapMode: Text.Wrap; Layout.fillWidth: true
        color: pluginContext && pluginContext.daemonAvailable
               ? "#34a853" : "#e05060"
    }

    MenuSeparator { Layout.fillWidth: true }

    // --- Account (leading-label form, macOS HIG) ---
    GridLayout {
        visible: pluginContext && !pluginContext.loggedIn
        columns: 2
        columnSpacing: 10
        Layout.fillWidth: true
        Label {
            text: qsTr("Email:")
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        }
        TextField {
            id: shopUser
            Layout.fillWidth: true
            text: pluginContext ? pluginContext.username : ""
            inputMethodHints: Qt.ImhEmailCharactersOnly
        }
        Label {
            text: qsTr("Password:")
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: shopPass
                Layout.fillWidth: true
                echoMode: TextInput.Password
                onAccepted: loginButton.clicked()
            }
            Button {
                id: loginButton
                text: qsTr("Log in")
                enabled: pluginContext && !pluginContext.busy &&
                         shopUser.text.length > 0 && shopPass.text.length > 0
                onClicked: { pluginContext.login(shopUser.text, shopPass.text)
                             shopPass.clear() }
            }
        }
        Item { }
        Label {
            text: qsTr("Your o-charts.org shop account.")
            font.pointSize: 10
            color: palette.placeholderText
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: pluginContext && pluginContext.loggedIn
        Label {
            text: qsTr("Account: ") + (pluginContext ? pluginContext.username : "")
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Label {
            text: qsTr("System: ") +
                  (pluginContext && pluginContext.systemName.length
                   ? pluginContext.systemName : qsTr("not identified"))
            color: pluginContext && pluginContext.systemName.length
                   ? palette.windowText : "#e08030"
        }
        Button {
            visible: pluginContext && !pluginContext.systemName.length
            text: qsTr("Identify this system")
            enabled: pluginContext && !pluginContext.busy &&
                     pluginContext.daemonAvailable
            onClicked: pluginContext.identifySystem()
        }
        Button {
            text: qsTr("Refresh chart list")
            enabled: pluginContext && !pluginContext.busy
            onClicked: pluginContext.refreshList()
        }
        Button {
            text: qsTr("Log out")
            onClicked: pluginContext.logout()
        }
    }

    // --- Purchased chart sets ---
    Frame {
        Layout.fillWidth: true
        Layout.preferredHeight: 230
        visible: pluginContext && pluginContext.loggedIn
        padding: 2
        ListView {
            id: setList
            anchors.fill: parent
            clip: true
            model: pluginContext ? pluginContext.charts : []
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: setList.width
                contentItem: RowLayout {
                    spacing: 8
                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Label {
                            text: modelData.chartName
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("Edition %1   expires %2%3")
                                  .arg(modelData.edition || "?")
                                  .arg(modelData.expiration || "?")
                                  .arg(modelData.supported ? ""
                                       : qsTr("   [raster — unsupported]"))
                            font.pointSize: 10
                            color: modelData.expired === "1"
                                   ? "#e05060" : palette.placeholderText
                        }
                    }
                    Label {
                        visible: modelData.assignedHere === true
                        text: qsTr("assigned here")
                        color: "#30a060"; font.pointSize: 10
                    }
                    Button {
                        text: modelData.assignedHere === true
                              ? qsTr("Install") : qsTr("Assign + install")
                        enabled: pluginContext && !pluginContext.busy &&
                                 modelData.supported === true &&
                                 modelData.expired !== "1"
                        onClicked: pluginContext.installChart(index)
                    }
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
    }
    Label {
        visible: pluginContext && pluginContext.loggedIn &&
                 pluginContext.charts.length === 0
        text: qsTr("No chart sets on this account yet — purchases at o-charts.org appear here.")
        wrapMode: Text.Wrap; Layout.fillWidth: true
        color: palette.placeholderText
    }
    ProgressBar {
        Layout.fillWidth: true
        visible: pluginContext && pluginContext.busy
        from: 0; to: 100
        value: pluginContext ? pluginContext.progress : 0
    }
    Label {
        text: pluginContext ? pluginContext.status : ""
        visible: text.length > 0
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        color: "#3b82f6"
    }
}
