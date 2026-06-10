import QtQuick

// The dashboard instrument strip: one pill per enabled instrument,
// top-left under the toolbar area. `pluginContext` is the DashboardContext.
Row {
    property var pluginContext: null
    x: 90
    y: 16
    spacing: 6

    Repeater {
        model: pluginContext ? pluginContext.enabled : []
        delegate: Rectangle {
            required property string modelData
            width: col.implicitWidth + 16
            height: col.implicitHeight + 10
            radius: 6
            color: "#cc101418"
            border.color: "#3affffff"
            Column {
                id: col
                anchors.centerIn: parent
                spacing: 0
                Text {
                    text: { switch (modelData) {
                        case "sog": return qsTr("SOG")
                        case "cog": return qsTr("COG")
                        case "hdg": return qsTr("HDG")
                        case "stw": return qsTr("STW")
                        case "awa": return qsTr("AWA/AWS")
                        case "twa": return qsTr("TWA/TWS")
                        default: return qsTr("POS") } }
                    color: "#8a98a8"; font.pointSize: 8
                }
                Text {
                    text: { if (!pluginContext) return "--"
                        switch (modelData) {
                        case "sog": return pluginContext.sog
                        case "cog": return pluginContext.cog
                        case "hdg": return pluginContext.hdg
                        case "stw": return pluginContext.stw
                        case "awa": return pluginContext.awaAws
                        case "twa": return pluginContext.twaTws
                        default: return pluginContext.position } }
                    color: "#e8f0ff"; font.pointSize: 11; font.bold: true
                }
            }
        }
    }
}
