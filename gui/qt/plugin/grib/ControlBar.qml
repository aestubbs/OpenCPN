import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// The GRIB control bar (wx GRIB control parity, v1): toggled by the 🌬
// toolbar button; file open, current-timestep readout (driven by the app
// time bar), layer toggles. Bottom-left, above the time bar.
Rectangle {
    property var pluginContext: null
    visible: pluginContext && pluginContext.controlsVisible
    anchors.left: parent ? parent.left : undefined
    anchors.bottom: parent ? parent.bottom : undefined
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
        CheckBox {
            text: qsTr("Wind")
            font.pointSize: 10
            checked: pluginContext ? pluginContext.showWind : true
            onToggled: if (pluginContext) pluginContext.showWind = checked
        }
        CheckBox {
            text: qsTr("Pressure")
            font.pointSize: 10
            checked: pluginContext ? pluginContext.showPressure : true
            onToggled: if (pluginContext) pluginContext.showPressure = checked
        }
    }
    FileDialog {
        id: gribBarFileDialog
        nameFilters: [qsTr("GRIB files (*.grb *.grb2 *.grib *.grib2 *.bz2 *.gz)"),
                      qsTr("All files (*)")]
        onAccepted: if (pluginContext) pluginContext.openFile(selectedFile)
    }
}
