import QtQuick

// The example plugin's HUD contribution: a clock pill, bottom-centre.
// `pluginContext` is assigned by the shell after load.
Rectangle {
    property var pluginContext: null
    anchors.bottom: parent ? parent.bottom : undefined
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
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
