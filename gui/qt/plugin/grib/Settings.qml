import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// GRIB (P4.5 v1): open a file, scrub the timeline, toggle the wind layer.
ColumnLayout {
    property var pluginContext: null
    spacing: 6

    RowLayout {
        Layout.fillWidth: true
        Button { text: qsTr("Open GRIB…"); onClicked: gribFileDialog.open() }
        Label {
            text: pluginContext && pluginContext.fileName.length
                  ? pluginContext.fileName : qsTr("(no file)")
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }
        CheckBox {
            text: qsTr("Wind")
            checked: pluginContext ? pluginContext.showWind : true
            onToggled: if (pluginContext) pluginContext.showWind = checked
        }
        CheckBox {
            text: qsTr("Pressure")
            checked: pluginContext ? pluginContext.showPressure : true
            onToggled: if (pluginContext) pluginContext.showPressure = checked
        }
    }
    FileDialog {
        id: gribFileDialog
        nameFilters: [qsTr("GRIB files (*.grb *.grb2 *.grib *.grib2 *.bz2 *.gz)"),
                      qsTr("All files (*)")]
        onAccepted: if (pluginContext) pluginContext.openFile(selectedFile)
    }
    RowLayout {
        Layout.fillWidth: true
        visible: pluginContext && pluginContext.timeSteps.length > 0
        Slider {
            Layout.fillWidth: true
            from: 0
            to: pluginContext ? pluginContext.timeSteps.length - 1 : 0
            stepSize: 1
            value: pluginContext ? pluginContext.timeIndex : 0
            onMoved: if (pluginContext) pluginContext.timeIndex = value
        }
        Label {
            text: pluginContext && pluginContext.timeIndex < pluginContext.timeSteps.length
                  ? pluginContext.timeSteps[pluginContext.timeIndex] : ""
            font.family: "monospace"
        }
    }
    MenuSeparator { Layout.fillWidth: true }
    Label { text: qsTr("Request a forecast (saildocs)"); font.bold: true }
    RowLayout {
        spacing: 8
        CheckBox { id: reqWind; text: qsTr("Wind"); checked: true }
        CheckBox { id: reqPres; text: qsTr("Pressure"); checked: true }
        CheckBox { id: reqWaves; text: qsTr("Waves") }
        CheckBox { id: reqPrecip; text: qsTr("Rain") }
        SpinBox {
            id: reqDays
            from: 1; to: 8; value: 3
        }
        Label { text: qsTr("days") }
        Button {
            text: qsTr("Email request…")
            onClicked: {
                const b = chart.viewBounds()
                requestEcho.text = pluginContext.requestGrib(
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
        color: "#3b82f6"
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
}
