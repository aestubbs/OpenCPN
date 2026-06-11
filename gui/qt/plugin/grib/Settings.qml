import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// GRIB preferences (wx GribSettingsDialog alignment): STAGED edits,
// applied by the host dialog's OK (apply()), discarded on Cancel. File
// opening / timestep / display toggles live on the CONTROL BAR, not here.
ColumnLayout {
    id: prefsPage
    property var pluginContext: null
    spacing: 8

    // --- staged state (loaded once when the page opens) ---
    property url stagedDir: pluginContext ? pluginContext.gribDir : ""
    function apply() {
        if (!pluginContext) return
        pluginContext.gribDir = stagedDir
    }

    Label { text: qsTr("Files"); font.bold: true }
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
        Button {
            text: qsTr("Choose…")
            onClicked: gribDirDialog.open()
        }
    }
    FolderDialog {
        id: gribDirDialog
        onAccepted: prefsPage.stagedDir = selectedFolder
    }
    Label {
        text: qsTr("The Open… menu on the GRIB control bar lists this folder, newest first.")
        font.pointSize: 10
        color: palette.placeholderText
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    MenuSeparator { Layout.fillWidth: true }

    Label { text: qsTr("Request a forecast (saildocs)"); font.bold: true }
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
    Label {
        text: pluginContext ? pluginContext.status : ""
        visible: text.length > 0
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        color: "#3b82f6"
    }
}
