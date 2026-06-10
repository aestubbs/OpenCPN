import QtQuick

// GRIB cursor readout: wind + pressure under the pointer (bottom-left
// pill; appears only while a GRIB file is loaded and the cursor is on
// the grid). `chart` resolves up the context chain (the HUD loads
// inside Main.qml's chart overlay area).
Rectangle {
    property var pluginContext: null
    readonly property string readout:
        pluginContext && !isNaN(chart.cursorLat)
            ? pluginContext.readoutAt(chart.cursorLat, chart.cursorLon) : ""
    visible: readout.length > 0
    anchors.left: parent ? parent.left : undefined
    anchors.bottom: parent ? parent.bottom : undefined
    anchors.leftMargin: 12
    anchors.bottomMargin: 64
    width: readoutText.implicitWidth + 18
    height: readoutText.implicitHeight + 8
    radius: height / 2
    color: "#cc101418"
    border.color: "#3affffff"
    Text {
        id: readoutText
        anchors.centerIn: parent
        text: "🌬 " + parent.readout
        color: "#e8f0ff"
        font.pointSize: 10
    }
}
