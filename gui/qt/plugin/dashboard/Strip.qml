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
            readonly property bool asDial:
                pluginContext && pluginContext.gauges &&
                (modelData === "cog" || modelData === "hdg")
            width: asDial ? 68 : col.implicitWidth + 16
            height: asDial ? 68 : col.implicitHeight + 10
            radius: 6
            color: "#cc101418"
            border.color: "#3affffff"
            Dial {
                visible: parent.asDial
                anchors.centerIn: parent
                angle: !pluginContext ? -1 :
                       (parent.modelData === "cog" ? pluginContext.cogDeg
                                                   : pluginContext.hdgDeg)
                label: parent.modelData === "cog"
                       ? (pluginContext ? pluginContext.cog : "--")
                       : (pluginContext ? pluginContext.hdg : "--")
            }
            Column {
                id: col
                visible: !parent.asDial
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
                        case "dpt": return qsTr("DEPTH")
                        case "mtw": return qsTr("WATER")
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
                        case "dpt": return pluginContext.depth
                        case "mtw": return pluginContext.waterTemp
                        default: return pluginContext.position } }
                    color: "#e8f0ff"; font.pointSize: 11; font.bold: true
                }
            }
        }
    }
}
