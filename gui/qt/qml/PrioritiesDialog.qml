import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import opencpn.qt

// Per-connection data priorities (P3.6, wx PriorityDlg parity): for each
// data category, the learned sources in priority order; move up/down to
// re-rank, Clear All to relearn. Native dialog window per the app-wide
// dialog convention.
Window {
    id: prioritiesDialog
    flags: Qt.Dialog
    modality: Qt.ApplicationModal
    title: qsTr("Data source priorities")
    width: 560; height: 420
    color: palette.window

    property int categoryIndex: 0
    property var sources: []

    function reload() {
        sources = CommPrioritiesModel.sourcesFor(categoryIndex)
        sourceList.currentIndex = -1
    }
    onVisibleChanged: if (visible) reload()
    onCategoryIndexChanged: reload()
    Connections {
        target: CommPrioritiesModel
        function onChanged() { prioritiesDialog.reload() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 10

        RowLayout {
            spacing: 8
            Label { text: qsTr("Category:") }
            ComboBox {
                Layout.fillWidth: true
                model: CommPrioritiesModel.categories()
                currentIndex: prioritiesDialog.categoryIndex
                onActivated: prioritiesDialog.categoryIndex = currentIndex
            }
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 2
            ListView {
                id: sourceList
                anchors.fill: parent
                clip: true
                model: prioritiesDialog.sources
                delegate: ItemDelegate {
                    required property string modelData
                    required property int index
                    width: sourceList.width
                    highlighted: ListView.isCurrentItem
                    onClicked: sourceList.currentIndex = index
                    contentItem: RowLayout {
                        Label {
                            text: (index + 1) + ".  " + modelData
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            font.bold: index === CommPrioritiesModel
                                           .activeIndex(prioritiesDialog.categoryIndex)
                        }
                        Label {
                            visible: index === CommPrioritiesModel
                                         .activeIndex(prioritiesDialog.categoryIndex)
                            text: qsTr("active")
                            color: "#30a060"
                            font.pointSize: 10
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }
        Label {
            visible: prioritiesDialog.sources.length === 0
            text: qsTr("No sources learned yet for this category — they appear as data arrives.")
            color: palette.placeholderText
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            Button {
                text: qsTr("Move up")
                enabled: sourceList.currentIndex > 0
                onClicked: {
                    const i = sourceList.currentIndex
                    CommPrioritiesModel.move(prioritiesDialog.categoryIndex, i, -1)
                    sourceList.currentIndex = i - 1
                }
            }
            Button {
                text: qsTr("Move down")
                enabled: sourceList.currentIndex >= 0 &&
                         sourceList.currentIndex < prioritiesDialog.sources.length - 1
                onClicked: {
                    const i = sourceList.currentIndex
                    CommPrioritiesModel.move(prioritiesDialog.categoryIndex, i, 1)
                    sourceList.currentIndex = i + 1
                }
            }
            Button {
                text: qsTr("Clear all")
                onClicked: CommPrioritiesModel.clearAll()
            }
            Item { Layout.fillWidth: true }
            DialogButtonBox {
                standardButtons: DialogButtonBox.Close
                onRejected: prioritiesDialog.close()
                onAccepted: prioritiesDialog.close()
            }
        }
    }
}
