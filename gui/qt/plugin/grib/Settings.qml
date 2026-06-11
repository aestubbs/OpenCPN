import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// GRIB Settings (wx GribSettingsDialog alignment): Data / Playback / GUI
// tabs; the Data tab reshapes per the selected type. Edits are STAGED and
// applied by the host dialog's OK/Apply (apply()).
ColumnLayout {
    id: prefsPage
    property var pluginContext: null
    spacing: 8

    // ---- staged state ----
    property url stagedDir: pluginContext ? pluginContext.gribDir : ""
    property int stagedDensity: pluginContext ? pluginContext.particleDensity : 5
    property bool stagedInterp: pluginContext ? pluginContext.interpolate : true
    property int stagedTransparency:
        pluginContext ? pluginContext.overlayTransparency : 55
    property var stagedUnits: ({})
    function apply() {
        if (!pluginContext) return
        pluginContext.gribDir = stagedDir
        pluginContext.particleDensity = stagedDensity
        pluginContext.interpolate = stagedInterp
        pluginContext.overlayTransparency = stagedTransparency
        for (const k in stagedUnits)
            pluginContext.setUnitFor(k, stagedUnits[k])
    }

    TabBar {
        id: prefsTabs
        Layout.fillWidth: true
        TabButton { text: qsTr("Data") }
        TabButton { text: qsTr("Playback") }
        TabButton { text: qsTr("GUI") }
        TabButton { text: qsTr("Request") }
    }

    StackLayout {
        Layout.fillWidth: true
        currentIndex: prefsTabs.currentIndex

        // ---- Data: per-type display options ----
        ColumnLayout {
            spacing: 8
            Label { text: qsTr("Data Display Options"); font.bold: true }
            RowLayout {
                spacing: 10
                ComboBox {
                    id: typeBox
                    implicitWidth: 160
                    model: pluginContext ? pluginContext.dataTypes : []
                    textRole: "label"
                    property string key: model.length
                        ? model[currentIndex].key : ""
                }
                Label { text: qsTr("Units") }
                ComboBox {
                    id: unitBox
                    implicitWidth: 100
                    visible: model.length > 0
                    model: pluginContext
                           ? pluginContext.unitOptions(typeBox.key) : []
                    currentIndex: {
                        const u = prefsPage.stagedUnits[typeBox.key] ||
                                  (pluginContext
                                   ? pluginContext.unitFor(typeBox.key) : "")
                        const i = model.indexOf(u)
                        return i < 0 ? 0 : i
                    }
                    onActivated: {
                        const m = Object.assign({}, prefsPage.stagedUnits)
                        m[typeBox.key] = model[currentIndex]
                        prefsPage.stagedUnits = m
                    }
                }
            }
            Label {
                text: {
                    switch (typeBox.key) {
                    case "wind": return qsTr("Wind shows as barbed arrows; optional colour wash via the Overlay selector; Particle Map animates the field.")
                    case "waves": return qsTr("Waves show as direction arrows with height.")
                    case "current": return qsTr("Current shows as direction arrows with speed.")
                    default: return qsTr("This field shows as numbers at grid points; it can also be the colour-wash Overlay.")
                    }
                }
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                font.pointSize: 10
                color: palette.placeholderText
            }
            RowLayout {
                visible: typeBox.key === "wind"
                spacing: 10
                Label { text: qsTr("Particle Map density") }
                Slider {
                    id: densitySlider
                    from: 1; to: 10; stepSize: 1
                    value: prefsPage.stagedDensity
                    onMoved: prefsPage.stagedDensity = value
                    Layout.preferredWidth: 180
                }
                Label { text: prefsPage.stagedDensity.toString() }
            }
            MenuSeparator { Layout.fillWidth: true }
            Label {
                text: qsTr("Transparency for all Overlay Maps")
                font.bold: true
            }
            RowLayout {
                spacing: 10
                Label { text: qsTr("Overlay Transparency (%)") }
                Slider {
                    from: 0; to: 100; stepSize: 1
                    value: prefsPage.stagedTransparency
                    onMoved: prefsPage.stagedTransparency = value
                    Layout.preferredWidth: 180
                }
                Label { text: prefsPage.stagedTransparency + " %" }
            }
        }

        // ---- Playback ----
        ColumnLayout {
            spacing: 8
            CheckBox {
                text: qsTr("Interpolate between forecast times")
                checked: prefsPage.stagedInterp
                onToggled: prefsPage.stagedInterp = checked
            }
            Label {
                text: qsTr("Play, speed and scrubbing live on the chart time bar; the weather follows it.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                font.pointSize: 10
                color: palette.placeholderText
            }
            Item { Layout.fillHeight: true }
        }

        // ---- GUI ----
        ColumnLayout {
            spacing: 8
            GridLayout {
                columns: 3
                columnSpacing: 10
                Layout.fillWidth: true
                Label {
                    text: qsTr("GRIB folder:")
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                }
                Label {
                    text: prefsPage.stagedDir.toString().length
                          ? prefsPage.stagedDir : qsTr("(Downloads)")
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                    color: palette.placeholderText
                }
                Button { text: qsTr("Choose…"); onClicked: gribDirDialog.open() }
            }
            FolderDialog {
                id: gribDirDialog
                onAccepted: prefsPage.stagedDir = selectedFolder
            }
            Label {
                text: qsTr("The flyout's Open… menu lists this folder, newest first. The Data-at-cursor panel is toggled on the flyout.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                font.pointSize: 10
                color: palette.placeholderText
            }
            Item { Layout.fillHeight: true }
        }

        // ---- Request (saildocs) ----
        ColumnLayout {
            spacing: 8
            RowLayout {
                spacing: 8
                ComboBox {
                    id: reqModel
                    implicitWidth: 110
                    property var matrix: ({ "GFS": [0.25, 0.5, 1.0],
                                            "ECMWF": [0.4],
                                            "ICON": [0.25],
                                            "ARPEGE": [0.5],
                                            "NAM": [0.25] })
                    model: Object.keys(matrix)
                    onActivated: reqRes.currentIndex = 0
                }
                ComboBox {
                    id: reqRes
                    implicitWidth: 80
                    model: reqModel.matrix[reqModel.currentText]
                    displayText: currentText + "°"
                }
                ComboBox {
                    id: reqInterval
                    implicitWidth: 70
                    model: [3, 6, 12]
                    displayText: currentText + " h"
                    currentIndex: 1
                }
                SpinBox { id: reqDays; from: 1; to: 8; value: 3 }
                Label { text: qsTr("days") }
            }
            RowLayout {
                spacing: 8
                CheckBox { id: reqWind; text: qsTr("Wind"); checked: true }
                CheckBox { id: reqPres; text: qsTr("Pressure"); checked: true }
                CheckBox { id: reqWaves; text: qsTr("Waves") }
                CheckBox { id: reqPrecip; text: qsTr("Rain") }
                Button {
                    text: qsTr("Email request…")
                    onClicked: {
                        const b = chart.viewBounds()
                        requestEcho.text = pluginContext.requestGrib(
                            reqModel.currentText,
                            reqRes.model[reqRes.currentIndex],
                            reqInterval.model[reqInterval.currentIndex],
                            b.north, b.south, b.east, b.west, reqDays.value,
                            reqWind.checked, reqPres.checked,
                            reqWaves.checked, reqPrecip.checked)
                    }
                }
            }
            Label {
                id: requestEcho
                visible: text.length > 0
                font.family: "monospace"; font.pointSize: 10
                color: palette.placeholderText
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Item { Layout.fillHeight: true }
        }
    }
}
