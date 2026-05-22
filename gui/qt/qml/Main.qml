// Main.qml -- top-level window for opencpn-qt.
//
// Hosts the ChartCanvas QQuickItem (the new Qt-Quick chart renderer) plus
// the QML HUD tier layered above it, and the application chrome (menu bar
// + toolbar). HUD items are bound to the Phase 1 QObject model classes
// (AisDecoder, comm_bridge, etc.) via Q_PROPERTY -- declarative bindings,
// animations for free.
//
// See docs/QT_MIGRATION_TASKS.md Phase 2 for the architecture.

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

import opencpn.qt

ApplicationWindow {
    id: root
    visible: true
    width: 1024
    height: 720
    title: qsTr("OpenCPN (Qt prototype)")

    // --- Application menu bar (skeleton; actions wired incrementally) ---
    menuBar: MenuBar {
        Menu {
            title: qsTr("File")
            MenuItem { text: qsTr("Quit"); onTriggered: Qt.quit() }
        }
        Menu {
            title: qsTr("View")
            MenuItem { text: qsTr("Zoom In"); onTriggered: chart.zoomIn() }
            MenuItem { text: qsTr("Zoom Out"); onTriggered: chart.zoomOut() }
            MenuItem { text: qsTr("Fit World"); onTriggered: chart.fitWorld() }
        }
        Menu {
            title: qsTr("Charts")
            MenuItem {
                text: qsTr("Base"); checkable: true
                checked: chart.displayCategory === 0
                onTriggered: chart.displayCategory = 0
            }
            MenuItem {
                text: qsTr("Standard"); checkable: true
                checked: chart.displayCategory === 1
                onTriggered: chart.displayCategory = 1
            }
            MenuItem {
                text: qsTr("All"); checkable: true
                checked: chart.displayCategory === 2
                onTriggered: chart.displayCategory = 2
            }
        }
    }

    // --- Toolbar: navigation + S-52 display category ---
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 4

            ToolButton { text: qsTr("−"); onClicked: chart.zoomOut() }
            ToolButton { text: qsTr("+"); onClicked: chart.zoomIn() }
            ToolButton { text: qsTr("Fit"); onClicked: chart.fitWorld() }

            ToolSeparator {}

            // S-52 display-category control (Base / Standard / All).
            Repeater {
                model: [ { label: qsTr("Base"), cat: 0 },
                         { label: qsTr("Standard"), cat: 1 },
                         { label: qsTr("All"), cat: 2 } ]
                delegate: ToolButton {
                    required property var modelData
                    text: modelData.label
                    checkable: true
                    checked: chart.displayCategory === modelData.cat
                    onClicked: chart.displayCategory = modelData.cat
                }
            }

            Item { Layout.fillWidth: true }  // spacer

            Label {
                text: qsTr("Phase 2 prototype")
                color: "#404040"
            }
        }
    }

    // Tier 1 + 2: world-anchored + display-anchored scene-graph subtrees,
    // both inside the ChartCanvas QQuickItem.
    ChartCanvas {
        id: chart
        anchors.fill: parent
        // Hand the S-52 engine to the canvas so it scans the chart set's
        // boundaries and streams cell content on demand (P2.x). `s52` is
        // the context property set in main.cpp.
        s52Engine: s52
    }

    // Tier 3: QML HUD -- live binding to the S-52 engine status
    // (Q_PROPERTY -> QML auto-rebinds on changed()).
    Text {
        id: s52Status
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
        text: s52 ? s52.status : qsTr("S-52: (no engine)")
        font.pointSize: 11
        color: s52 && s52.ok ? "#006400" : "#a00000"
    }
}
