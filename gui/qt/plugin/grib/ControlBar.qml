import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// The GRIB control bar (wx parity, redesigned for readability): a light
// opaque panel top-left (clear of the chart pills), with prev/step/next,
// per-type toggle chips, overlay choice and quick actions. Toggled by
// the 🌬 toolbar button.
Item {
    property var pluginContext: null

    Rectangle {
        visible: pluginContext && pluginContext.controlsVisible
        x: 76
        y: 16
        width: barCol.implicitWidth + 20
        height: barCol.implicitHeight + 14
        radius: 6
        color: Qt.rgba(0.93, 0.93, 0.95, 0.92)
        border.color: Qt.rgba(0, 0, 0, 0.35)
        border.width: 1

        ColumnLayout {
            id: barCol
            anchors.centerIn: parent
            spacing: 4

            // Row 1: file + timestep navigation.
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
                            onTriggered: gribBarFileDialog.open()
                        }
                    }
                }
                Label {
                    text: pluginContext && pluginContext.fileName.length
                          ? pluginContext.fileName : qsTr("— no GRIB loaded —")
                    font.pointSize: 11
                    color: "#1b1d21"
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 200
                }
                ToolSeparator { }
                // Timestep prev / current / next: drives the app TIME BAR,
                // which in turn drives the weather (single time source).
                ToolButton {
                    text: "⏮"
                    enabled: pluginContext && pluginContext.timeIndex > 0
                    onClicked: pluginContext.stepTimeline(-1)
                }
                Label {
                    text: pluginContext &&
                          pluginContext.timeIndex < pluginContext.timeSteps.length
                          ? pluginContext.timeSteps[pluginContext.timeIndex]
                          : "--"
                    font.pointSize: 11; font.bold: true
                    color: "#1b1d21"
                }
                ToolButton {
                    text: "⏭"
                    enabled: pluginContext && pluginContext.timeIndex <
                             pluginContext.timeSteps.length - 1
                    onClicked: pluginContext.stepTimeline(1)
                }
            }

            // Row 2: what to show.
            RowLayout {
                spacing: 6
                Repeater {
                    model: pluginContext ? pluginContext.dataTypes : []
                    delegate: Button {
                        required property var modelData
                        visible: modelData.available
                        checkable: true
                        checked: modelData.shown
                        text: modelData.label
                        font.pointSize: 10
                        padding: 5
                        onClicked: pluginContext.setTypeShown(modelData.key,
                                                              checked)
                    }
                }
                ToolSeparator { }
                Label {
                    text: qsTr("Overlay")
                    font.pointSize: 10; color: "#5a5d63"
                }
                ComboBox {
                    id: overlayBox
                    font.pointSize: 10
                    implicitWidth: 120
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
                Button {
                    checkable: true
                    text: qsTr("Particles")
                    font.pointSize: 10
                    padding: 5
                    checked: pluginContext ? pluginContext.particles : false
                    onClicked: if (pluginContext) pluginContext.particles = checked
                }
                ComboBox {
                    id: altBox
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
        }
    }
    FileDialog {
        id: gribBarFileDialog
        nameFilters: [qsTr("GRIB files (*.grb *.grb2 *.grib *.grib2 *.bz2 *.gz)"),
                      qsTr("All files (*)")]
        onAccepted: if (pluginContext) pluginContext.openFile(selectedFile)
    }
}
