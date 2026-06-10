import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import opencpn.qt

// The small HUD stats panel (the built-in replacement for the dashboard
// plugin): a draggable vertical list of live readouts over the chart,
// top-right by default. The ⚙ picker chooses which stats show
// (UIConfig.hudStats, persisted); position persists as canvas fractions.
Rectangle {
    id: statsPanel

    // All available stats, in display order. Bus-derived entries (depth,
    // water temperature) read the canvas's DPT/MTW parse.
    readonly property var catalog: [
        { key: "pos", label: qsTr("POS") },
        { key: "sog", label: qsTr("SOG") },
        { key: "cog", label: qsTr("COG") },
        { key: "hdg", label: qsTr("HDG") },
        { key: "stw", label: qsTr("STW") },
        { key: "awa", label: qsTr("AWA/AWS") },
        { key: "twa", label: qsTr("TWA/TWS") },
        { key: "dpt", label: qsTr("DEPTH") },
        { key: "mtw", label: qsTr("WATER") },
    ]
    readonly property var nav: chart.navState

    function valueFor(key) {
        const n = nav
        switch (key) {
        case "pos": return n && n.ownShipValid
            ? DisplayConfig.formatLatLon(n.lat, n.lon) : "--"
        case "sog": return n && n.ownShipValid ? n.sog.toFixed(1) + " kn" : "--"
        case "cog": return n && n.ownShipValid
            ? ("00" + Math.round(n.cog)).slice(-3) + "°" : "--"
        case "hdg": return n && n.hdgValid
            ? ("00" + Math.round(n.hdg)).slice(-3) + "°" : "--"
        case "stw": return n && n.stw >= 0 ? n.stw.toFixed(1) + " kn" : "--"
        case "awa": return n && n.awaValid
            ? Math.round(n.awa) + "° / " + n.aws.toFixed(1) + " kn" : "--"
        case "twa": return n && n.twaValid
            ? Math.round(n.twa) + "° / " + n.tws.toFixed(1) + " kn" : "--"
        case "dpt": return chart.depthText
        case "mtw": return chart.waterTempText
        default: return "--"
        }
    }

    visible: UIConfig.hudStats.length > 0
    width: statsCol.implicitWidth + 20
    height: statsCol.implicitHeight + 16
    radius: 8
    color: "#cc101418"
    border.color: "#3affffff"

    // Default dock: top-right (clear of the MUI bar); dragging anywhere
    // re-pins via persisted canvas fractions.
    x: UIConfig.hudStatsX >= 0
       ? UIConfig.hudStatsX * (parent.width - width)
       : parent.width - width - 12
    y: UIConfig.hudStatsY >= 0
       ? UIConfig.hudStatsY * (parent.height - height) : 44

    MouseArea {
        anchors.fill: parent
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        drag.target: statsPanel
        drag.minimumX: 0
        drag.maximumX: statsPanel.parent.width - statsPanel.width
        drag.minimumY: 0
        drag.maximumY: statsPanel.parent.height - statsPanel.height
        onReleased: {
            UIConfig.hudStatsX = statsPanel.x /
                Math.max(1, statsPanel.parent.width - statsPanel.width)
            UIConfig.hudStatsY = statsPanel.y /
                Math.max(1, statsPanel.parent.height - statsPanel.height)
        }
    }

    ColumnLayout {
        id: statsCol
        anchors.centerIn: parent
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ToolButton {
                text: "⚙"
                padding: 0
                implicitWidth: 18; implicitHeight: 18
                font.pointSize: 10
                onClicked: pickerPopup.open()
            }
        }
        Repeater {
            model: statsPanel.catalog.filter(
                       (s) => UIConfig.hudStats.indexOf(s.key) >= 0)
            delegate: ColumnLayout {
                required property var modelData
                spacing: 0
                Label {
                    text: modelData.label
                    color: "#8a98a8"; font.pointSize: 8
                }
                Label {
                    text: statsPanel.valueFor(modelData.key)
                    color: "#e8f0ff"; font.pointSize: 13; font.bold: true
                }
            }
        }
    }

    // The stat picker: which of the available values (incl. those seen
    // on the NMEA bus) show on the panel.
    Popup {
        id: pickerPopup
        x: parent.width - width
        y: 22
        padding: 10
        ColumnLayout {
            spacing: 2
            Label { text: qsTr("Show on HUD"); font.bold: true }
            Repeater {
                model: statsPanel.catalog
                delegate: CheckBox {
                    required property var modelData
                    text: modelData.label
                    padding: 2
                    checked: UIConfig.hudStats.indexOf(modelData.key) >= 0
                    onToggled: {
                        var l = UIConfig.hudStats.slice()
                        if (checked) l.push(modelData.key)
                        else l.splice(l.indexOf(modelData.key), 1)
                        // Keep catalog order.
                        UIConfig.hudStats = statsPanel.catalog
                            .map((s) => s.key).filter((k) => l.indexOf(k) >= 0)
                    }
                }
            }
        }
    }
}
