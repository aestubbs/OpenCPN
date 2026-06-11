import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// The GRIB flyout (press-and-hold / right-click the 🌬 tool): the wx
// floating dialog's controls as a compact panel beside the toolbar.
// Time itself lives on the chart time bar; ⏮⏭ snap it between steps.
Item {
    property var pluginContext: null

    // Click-away closes the flyout.
    MouseArea {
        anchors.fill: parent
        visible: pluginContext && pluginContext.controlsVisible
        onPressed: (m) => { pluginContext.controlsVisible = false; m.accepted = false }
    }

    Rectangle {
        visible: pluginContext && pluginContext.controlsVisible
        x: 76
        y: 120
        width: flyCol.implicitWidth + 22
        height: flyCol.implicitHeight + 18
        radius: 8
        color: Qt.rgba(0.95, 0.95, 0.97, 0.97)
        border.color: Qt.rgba(0, 0, 0, 0.35)
        border.width: 1

        ColumnLayout {
            id: flyCol
            anchors.centerIn: parent
            spacing: 5

            // Master switch -- explicit, never silently toggled.
            RowLayout {
                spacing: 6
                Switch {
                    text: qsTr("Show weather")
                    font.pointSize: 11
                    checked: pluginContext ? pluginContext.masterEnabled : true
                    onToggled: if (pluginContext)
                                   pluginContext.masterEnabled = checked
                }
                Item { Layout.fillWidth: true }
            }

            // File + step row.
            RowLayout {
                spacing: 6
                Button {
                    text: qsTr("Open…")
                    font.pointSize: 11
                    onClicked: dirMenu.open()
                    Menu {
                        id: dirMenu
                        Instantiator {
                            model: pluginContext ? pluginContext.dirFiles : []
                            delegate: MenuItem {
                                required property var modelData
                                text: modelData.name + "   " + modelData.date
                                onTriggered: pluginContext.openFile(modelData.path)
                            }
                            onObjectAdded: (i, o) => dirMenu.insertItem(i, o)
                            onObjectRemoved: (i, o) => dirMenu.removeItem(o)
                        }
                        MenuSeparator { }
                        MenuItem {
                            text: qsTr("Browse…")
                            onTriggered: gribFlyFileDialog.open()
                        }
                    }
                }
                Label {
                    text: pluginContext && pluginContext.fileName.length
                          ? pluginContext.fileName : qsTr("no file")
                    font.pointSize: 10
                    color: "#1b1d21"
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 150
                }
                ToolButton {
                    text: "⏮"
                    enabled: pluginContext && pluginContext.timeIndex > 0
                    onClicked: pluginContext.stepTimeline(-1)
                }
                Label {
                    text: pluginContext &&
                          pluginContext.timeIndex < pluginContext.timeSteps.length
                          ? pluginContext.timeSteps[pluginContext.timeIndex] : "--"
                    font.pointSize: 10; font.bold: true
                    color: "#1b1d21"
                }
                ToolButton {
                    text: "⏭"
                    enabled: pluginContext && pluginContext.timeIndex <
                             pluginContext.timeSteps.length - 1
                    onClicked: pluginContext.stepTimeline(1)
                }
            }

            MenuSeparator { Layout.fillWidth: true }

            // Type toggles, two columns.
            GridLayout {
                columns: 2
                columnSpacing: 12
                rowSpacing: 0
                Repeater {
                    model: pluginContext ? pluginContext.dataTypes : []
                    delegate: CheckBox {
                        required property var modelData
                        visible: modelData.available
                        text: modelData.label
                        font.pointSize: 11
                        padding: 3
                        checked: modelData.shown
                        onToggled: pluginContext.setTypeShown(modelData.key,
                                                              checked)
                    }
                }
            }

            MenuSeparator { Layout.fillWidth: true }

            RowLayout {
                spacing: 8
                Label { text: qsTr("Overlay:"); font.pointSize: 11 }
                ComboBox {
                    font.pointSize: 10
                    implicitWidth: 115
                    model: {
                        const l = [{ key: "", label: qsTr("None") }]
                        const types = pluginContext ? pluginContext.dataTypes : []
                        for (let i = 0; i < types.length; ++i)
                            if (types[i].available && types[i].key !== "pressure")
                                l.push(types[i])
                        return l
                    }
                    textRole: "label"
                    currentIndex: {
                        for (let i = 0; i < model.length; ++i)
                            if (model[i].key === (pluginContext
                                                  ? pluginContext.overlayKey : ""))
                                return i
                        return 0
                    }
                    onActivated: pluginContext.overlayKey = model[currentIndex].key
                }
                ComboBox {
                    font.pointSize: 10
                    implicitWidth: 95
                    visible: model.length > 1
                    model: {
                        const l = []
                        const alts = pluginContext ? pluginContext.altitudes : []
                        for (let i = 0; i < alts.length; ++i)
                            if (alts[i].available) l.push(alts[i])
                        return l
                    }
                    textRole: "label"
                    currentIndex: {
                        for (let i = 0; i < model.length; ++i)
                            if (model[i].hpa === (pluginContext
                                                  ? pluginContext.windAltitude : 0))
                                return i
                        return 0
                    }
                    onActivated: pluginContext.windAltitude = model[currentIndex].hpa
                }
            }
            RowLayout {
                spacing: 8
                CheckBox {
                    text: qsTr("Particles")
                    font.pointSize: 11
                    padding: 3
                    checked: pluginContext ? pluginContext.particles : false
                    onToggled: if (pluginContext) pluginContext.particles = checked
                }
                CheckBox {
                    text: qsTr("Data at cursor")
                    font.pointSize: 11
                    padding: 3
                    checked: pluginContext ? pluginContext.cursorPanelVisible : false
                    onToggled: if (pluginContext)
                                   pluginContext.cursorPanelVisible = checked
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Settings…")
                    font.pointSize: 10
                    onClicked: {
                        pluginContext.controlsVisible = false
                        optionsWindow.openPluginPrefs("GRIB")
                    }
                }
            }
        }
    }
    FileDialog {
        id: gribFlyFileDialog
        nameFilters: [qsTr("GRIB files (*.grb *.grb2 *.grib *.grib2 *.bz2 *.gz)"),
                      qsTr("All files (*)")]
        onAccepted: if (pluginContext) pluginContext.openFile(selectedFile)
    }
}
