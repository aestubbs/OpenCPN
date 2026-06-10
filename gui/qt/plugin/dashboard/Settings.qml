import QtQuick
import QtQuick.Controls

// Dashboard settings: per-instrument visibility checkboxes.
Column {
    property var pluginContext: null
    spacing: 2
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
                default: return qsTr("Position") } }
            checked: pluginContext &&
                     pluginContext.enabled.indexOf(modelData) >= 0
            onToggled: pluginContext.setInstrumentEnabled(modelData, checked)
        }
    }
}
