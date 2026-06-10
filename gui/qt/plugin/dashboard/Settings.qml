import QtQuick
import QtQuick.Controls

// Dashboard settings: per-instrument visibility checkboxes.
Column {
    property var pluginContext: null
    spacing: 2
    Row {
        spacing: 8
        Label {
            text: qsTr("Position:")
            anchors.verticalCenter: parent.verticalCenter
        }
        ComboBox {
            model: [qsTr("Top left"), qsTr("Top right"),
                    qsTr("Bottom left"), qsTr("Bottom right")]
            currentIndex: { switch (pluginContext ? pluginContext.corner : "tl") {
                case "tr": return 1; case "bl": return 2
                case "br": return 3; default: return 0 } }
            onActivated: if (pluginContext)
                pluginContext.corner = ["tl", "tr", "bl", "br"][currentIndex]
        }
        CheckBox {
            text: qsTr("Stack vertically")
            checked: pluginContext ? pluginContext.vertical : false
            onToggled: if (pluginContext) pluginContext.vertical = checked
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    CheckBox {
        text: qsTr("Compass dials for COG / heading")
        checked: pluginContext ? pluginContext.gauges : false
        onToggled: if (pluginContext) pluginContext.gauges = checked
    }
    Repeater {
        model: pluginContext ? pluginContext.allInstruments() : []
        delegate: CheckBox {
            required property string modelData
            text: { switch (modelData) {
                case "sog": return qsTr("Speed over ground")
                case "cog": return qsTr("Course over ground")
                case "hdg": return qsTr("Heading")
                case "stw": return qsTr("Speed through water")
                case "awa": return qsTr("Apparent wind")
                case "twa": return qsTr("True wind")
                case "dpt": return qsTr("Depth")
                case "mtw": return qsTr("Water temperature")
                default: return qsTr("Position") } }
            checked: pluginContext &&
                     pluginContext.enabled.indexOf(modelData) >= 0
            onToggled: pluginContext.setInstrumentEnabled(modelData, checked)
        }
    }
}
