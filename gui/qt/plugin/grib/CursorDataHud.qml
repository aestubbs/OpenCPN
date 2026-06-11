import QtQuick
import QtQuick.Controls

// Data-at-cursor HUD (wx "Data at cursor position", as its own panel):
// per-type rows for the position under the pointer; toggled from the
// GRIB flyout. Bottom-left, above the chart pills.
Item {
    property var pluginContext: null
    readonly property var rows:
        pluginContext && pluginContext.cursorPanelVisible &&
        !isNaN(chart.cursorLat)
            ? pluginContext.cursorRows(chart.cursorLat, chart.cursorLon) : []

    Rectangle {
        visible: pluginContext && pluginContext.cursorPanelVisible
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 12
        anchors.bottomMargin: 64
        width: hudGrid.implicitWidth + 20
        height: hudGrid.implicitHeight + 14
        radius: 6
        color: Qt.rgba(0.95, 0.95, 0.97, 0.95)
        border.color: Qt.rgba(0, 0, 0, 0.35)

        Grid {
            id: hudGrid
            anchors.centerIn: parent
            columns: 2
            columnSpacing: 12
            rowSpacing: 1
            Repeater {
                model: {
                    // rows are "Label\tvalue" -> two cells each.
                    const cells = []
                    for (let i = 0; i < rows.length; ++i) {
                        const p = rows[i].split("\t")
                        cells.push(p[0]); cells.push(p[1] || "")
                    }
                    return cells.length ? cells
                                        : [qsTr("GRIB"), qsTr("no data here")]
                }
                delegate: Text {
                    required property string modelData
                    required property int index
                    text: modelData
                    font.pointSize: 11
                    font.bold: index % 2 === 1
                    color: "#1b1d21"
                }
            }
        }
    }
}
