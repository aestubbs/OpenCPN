// Main.qml -- top-level window for opencpn-qt.
//
// Hosts the ChartCanvas QQuickItem (the new Qt-Quick chart renderer) plus
// the QML HUD tier layered above it. HUD items are bound to the Phase 1
// QObject model classes (AisDecoder, comm_bridge, etc.) via Q_PROPERTY --
// declarative bindings, animations for free.
//
// See docs/QT_MIGRATION_TASKS.md Phase 2 for the architecture.

import QtQuick
import QtQuick.Window
import QtQuick.Controls

import opencpn.qt

ApplicationWindow {
    id: root
    visible: true
    width: 1024
    height: 720
    title: qsTr("OpenCPN (Qt prototype)")

    // Tier 1 + 2: world-anchored + display-anchored scene-graph subtrees,
    // both inside the ChartCanvas QQuickItem.
    ChartCanvas {
        id: chart
        anchors.fill: parent
    }

    // Tier 3: QML HUD. Placeholder readout demonstrating the binding path
    // that the AIS / nav-data / depth view-models will use in Phase 3.
    Text {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        text: qsTr("Phase 2 prototype")
        font.pointSize: 14
        color: "#202020"
    }
}
