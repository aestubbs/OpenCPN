import QtQuick
import QtQuick.Controls

// The example plugin's settings page (Options > Plugins).
Column {
    property var pluginContext: null
    spacing: 4
    Label {
        text: qsTr("The example plugin is running; its HUD clock reads ") +
              (pluginContext ? pluginContext.clockText : "--")
        wrapMode: Text.Wrap
        width: parent.width
    }
}
