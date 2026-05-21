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

    // Tier 3: QML HUD. Two text items: the prototype label and a live
    // binding to the S-52 engine status (Q_PROPERTY -> QML auto-rebinds
    // on changed()). Demonstrates the binding path that AIS / nav-data /
    // depth view-models will use in Phase 3.
    Text {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        text: qsTr("Phase 2 prototype")
        font.pointSize: 14
        color: "#202020"
    }
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
