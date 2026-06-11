import QtQuick

// The example plugin's HUD contribution. CONTRACT: the root Item fills
// the chart overlay area (the host resizes it); anchor visuals inside.
Item {
    property var pluginContext: null

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 44
        width: clockText.implicitWidth + 18
        height: clockText.implicitHeight + 8
        radius: height / 2
        color: "#cc101418"
        border.color: "#3affffff"
        Text {
            id: clockText
            anchors.centerIn: parent
            text: "⏱ " + (pluginContext ? pluginContext.clockText : "--:--:--")
            color: "#e8f0ff"
            font.pointSize: 10
        }
    }
}
