import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// The GRIB control bar (wx GRIB control parity, v1): toggled by the 🌬
// toolbar button; file open, current-timestep readout (driven by the app
// time bar), layer toggles. Bottom-left, above the time bar.
Item {
    property var pluginContext: null

    Rectangle {
    visible: pluginContext && pluginContext.controlsVisible
    anchors.left: parent.left
    anchors.bottom: parent.bottom
    anchors.leftMargin: 76
    anchors.bottomMargin: 12
    width: barRow.implicitWidth + 20
    height: barRow.implicitHeight + 12
    radius: 8
    color: "#cc101418"
    border.color: "#3affffff"

    RowLayout {
        id: barRow
        anchors.centerIn: parent
        spacing: 8
        Button {
            text: qsTr("Open…")
            font.pointSize: 10
            onClicked: gribBarFileDialog.open()
        }
        Label {
            text: pluginContext && pluginContext.fileName.length
                  ? pluginContext.fileName : qsTr("no GRIB loaded")
            color: "#e8f0ff"
            font.pointSize: 10
            elide: Text.ElideMiddle
            Layout.maximumWidth: 180
        }
        Label {
            visible: pluginContext && pluginContext.timeSteps.length > 0
            text: pluginContext &&
                  pluginContext.timeIndex < pluginContext.timeSteps.length
                  ? pluginContext.timeSteps[pluginContext.timeIndex] : ""
            color: "#8fd14f"
            font.pointSize: 10; font.bold: true
        }
        ComboBox {
            id: overlayBox
            font.pointSize: 10
            implicitWidth: 130
            // "Overlay: none" + every available type.
            model: {
                const l = [{ key: "", label: qsTr("No overlay") }]
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
        Repeater {
            model: pluginContext ? pluginContext.dataTypes : []
            delegate: CheckBox {
                required property var modelData
                visible: modelData.available
                text: modelData.label
                font.pointSize: 10
                checked: modelData.shown
                onToggled: pluginContext.setTypeShown(modelData.key, checked)
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
}
