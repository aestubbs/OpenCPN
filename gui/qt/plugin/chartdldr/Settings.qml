import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// Chart downloader (P4.4): catalog URL -> chart list -> download+extract.
ColumnLayout {
    property var pluginContext: null
    spacing: 6

    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Catalog:") }
        TextField {
            Layout.fillWidth: true
            text: pluginContext ? pluginContext.catalogUrl : ""
            selectByMouse: true
            onEditingFinished: if (pluginContext) pluginContext.catalogUrl = text
        }
        Button {
            text: qsTr("Load")
            enabled: pluginContext && !pluginContext.busy
            onClicked: pluginContext.loadCatalog()
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: qsTr("Into: ") + (pluginContext && pluginContext.targetFolder.toString().length
                  ? pluginContext.targetFolder : qsTr("(Downloads)"))
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }
        Button {
            text: qsTr("Choose folder…")
            onClicked: folderDialog.open()
        }
    }
    FolderDialog {
        id: folderDialog
        onAccepted: if (pluginContext) pluginContext.targetFolder = selectedFolder
    }
    Frame {
        Layout.fillWidth: true
        Layout.preferredHeight: 180
        padding: 2
        ListView {
            id: chartList
            anchors.fill: parent
            clip: true
            model: pluginContext ? pluginContext.charts : []
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: chartList.width
                contentItem: RowLayout {
                    Label {
                        text: modelData.title
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: modelData.dateText || ""
                        color: palette.placeholderText
                        font.pointSize: 9
                    }
                    Button {
                        text: qsTr("Download")
                        enabled: pluginContext && !pluginContext.busy
                        onClicked: pluginContext.downloadChart(index)
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
