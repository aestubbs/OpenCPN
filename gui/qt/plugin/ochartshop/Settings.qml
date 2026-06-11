import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// o-charts shop (P4.7): account login -> system registration -> purchased
// chart sets with one-click install.
ColumnLayout {
    property var pluginContext: null
    spacing: 6

    // --- Account row ---
    RowLayout {
        Layout.fillWidth: true
        visible: pluginContext && !pluginContext.loggedIn
        TextField {
            id: shopUser
            Layout.fillWidth: true
            placeholderText: qsTr("o-charts.org email")
            text: pluginContext ? pluginContext.username : ""
        }
        TextField {
            id: shopPass
            Layout.fillWidth: true
            placeholderText: qsTr("Password")
            echoMode: TextInput.Password
        }
        Button {
            text: qsTr("Log in")
            enabled: pluginContext && !pluginContext.busy &&
                     shopUser.text.length > 0 && shopPass.text.length > 0
            onClicked: { pluginContext.login(shopUser.text, shopPass.text)
                         shopPass.clear() }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: pluginContext && pluginContext.loggedIn
        Label {
            text: qsTr("Account: ") + (pluginContext ? pluginContext.username : "")
            Layout.fillWidth: true
        }
        Label {
            text: qsTr("System: ") +
                  (pluginContext && pluginContext.systemName.length
                   ? pluginContext.systemName : qsTr("(not identified)"))
            color: pluginContext && pluginContext.systemName.length
                   ? palette.windowText : "#e08030"
        }
        Button {
            visible: pluginContext && !pluginContext.systemName.length
            text: qsTr("Identify this system")
            enabled: !pluginContext.busy
            onClicked: pluginContext.identifySystem()
        }
        Button {
            text: qsTr("Refresh")
            enabled: pluginContext && !pluginContext.busy
            onClicked: pluginContext.refreshList()
        }
        Button {
            text: qsTr("Log out")
            onClicked: pluginContext.logout()
        }
    }

    // --- Chart sets ---
    Frame {
        Layout.fillWidth: true
        Layout.preferredHeight: 200
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
    ProgressBar {
        Layout.fillWidth: true
        visible: pluginContext && pluginContext.busy
        from: 0; to: 100
        value: pluginContext ? pluginContext.progress : 0
    }
    Label {
        text: pluginContext ? pluginContext.status : ""
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        color: "#3b82f6"
    }
}
