// Main.qml -- top-level application shell for opencpn-qt (P3.1 / P3.5).
//
// Structure: a touch-friendly header ToolBar with the primary navigation
// controls + a menu button that opens a slide-out Drawer holding the S-52
// display / detail / demo controls; the ChartCanvas QQuickItem fills the
// central area; a footer status bar shows engine + nav status. The QML HUD
// tier (nav readouts) is layered above the chart. All controls bind to the
// ChartCanvas Q_PROPERTYs / the nav view-model -- declarative, no imperative
// plumbing.
//
// See docs/QT_MIGRATION_TASKS.md Phase 3 for the architecture.

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

    // Minimum touch target (logical px) -- sizes the toolbar + drawer
    // controls for finger use (P3.5).
    readonly property int touchSize: 44

    // --- Header: touch-friendly primary toolbar ---------------------------
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 6

            ToolButton {
                text: "☰"  // hamburger
                font.pointSize: 16
                Layout.preferredHeight: root.touchSize
                Layout.preferredWidth: root.touchSize
                onClicked: controlsDrawer.open()
            }

            ToolSeparator {}

            ToolButton {
                text: "−"  // minus
                font.pointSize: 18
                Layout.preferredHeight: root.touchSize
                Layout.preferredWidth: root.touchSize
                onClicked: chart.zoomOut()
            }
            ToolButton {
                text: "+"
                font.pointSize: 18
                Layout.preferredHeight: root.touchSize
                Layout.preferredWidth: root.touchSize
                onClicked: chart.zoomIn()
            }
            ToolButton {
                text: qsTr("Fit")
                font.pointSize: 13
                Layout.preferredHeight: root.touchSize
                onClicked: chart.fitWorld()
            }

            Item { Layout.fillWidth: true }  // spacer

            ToolButton {
                text: chart.demoMode ? qsTr("Demo: on") : qsTr("Demo: off")
                font.pointSize: 13
                checkable: true
                checked: chart.demoMode
                Layout.preferredHeight: root.touchSize
                onClicked: chart.demoMode = checked
            }
        }
    }

    // --- Slide-out controls drawer (the shell's panel area) ---------------
    Drawer {
        id: controlsDrawer
        width: Math.min(300, root.width * 0.85)
        height: root.height
        edge: Qt.LeftEdge

        // Navigation menu -- destinations open dialogs/panels; the shell
        // keeps the chart full-bleed behind.
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 2

            Label {
                text: qsTr("OpenCPN")
                font.pointSize: 16; font.bold: true
                Layout.margins: 8
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#40808080" }

            ItemDelegate {
                text: qsTr("Display settings…")
                font.pointSize: 14
                Layout.fillWidth: true; Layout.preferredHeight: root.touchSize
                onClicked: { settingsDialog.open(); controlsDrawer.close() }
            }
            ItemDelegate {
                text: qsTr("Routes & marks…")
                font.pointSize: 14
                Layout.fillWidth: true; Layout.preferredHeight: root.touchSize
                onClicked: { routeManagerDialog.open(); controlsDrawer.close() }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#40808080" }

            Switch {
                text: qsTr("Demo mode"); font.pointSize: 14
                Layout.fillWidth: true; Layout.preferredHeight: root.touchSize
                leftPadding: 16
                checked: chart.demoMode
                onToggled: chart.demoMode = checked
            }
            ItemDelegate {
                text: qsTr("Drop demo here")
                font.pointSize: 14
                Layout.fillWidth: true; Layout.preferredHeight: root.touchSize
                onClicked: { chart.dropDemoHere(); controlsDrawer.close() }
            }

            Item { Layout.fillHeight: true }  // push Quit to the bottom

            ItemDelegate {
                text: qsTr("Quit")
                font.pointSize: 14
                Layout.fillWidth: true; Layout.preferredHeight: root.touchSize
                onClicked: Qt.quit()
            }
        }
    }

    // --- Display settings dialog (P3.6) -----------------------------------
    Dialog {
        id: settingsDialog
        title: qsTr("Display settings")
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width * 0.9, 420)
        standardButtons: Dialog.Close

        ColumnLayout {
            width: parent.width
            spacing: 8

            Label {
                text: qsTr("Chart display category")
                font.pointSize: 14; font.bold: true
            }
            ButtonGroup { id: catGroup }
            Repeater {
                model: [ { label: qsTr("Base"), cat: 0 },
                         { label: qsTr("Standard"), cat: 1 },
                         { label: qsTr("All"), cat: 2 } ]
                delegate: RadioButton {
                    required property var modelData
                    text: modelData.label
                    font.pointSize: 13
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.touchSize
                    ButtonGroup.group: catGroup
                    checked: chart.displayCategory === modelData.cat
                    onClicked: chart.displayCategory = modelData.cat
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#40808080" }

            Label { text: qsTr("Detail"); font.pointSize: 14; font.bold: true }
            Switch {
                text: qsTr("Soundings"); font.pointSize: 13
                Layout.fillWidth: true; Layout.preferredHeight: root.touchSize
                checked: chart.showSoundings
                onToggled: chart.showSoundings = checked
            }
            Switch {
                text: qsTr("Text labels"); font.pointSize: 13
                Layout.fillWidth: true; Layout.preferredHeight: root.touchSize
                checked: chart.showText
                onToggled: chart.showText = checked
            }
            Switch {
                text: qsTr("Lights"); font.pointSize: 13
                Layout.fillWidth: true; Layout.preferredHeight: root.touchSize
                checked: chart.showLights
                onToggled: chart.showLights = checked
            }
            Switch {
                text: qsTr("Buoys & beacons"); font.pointSize: 13
                Layout.fillWidth: true; Layout.preferredHeight: root.touchSize
                checked: chart.showBuoys
                onToggled: chart.showBuoys = checked
            }
        }
    }

    // --- Route & mark manager dialog (P3.7) -------------------------------
    Dialog {
        id: routeManagerDialog
        title: qsTr("Routes & marks")
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width * 0.9, 460)
        height: Math.min(root.height * 0.85, 560)
        standardButtons: Dialog.Close

        readonly property var rl: chart.routeList

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            // Layer visibility.
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Switch {
                    text: qsTr("Routes"); font.pointSize: 12
                    checked: chart.showRoutes
                    onToggled: chart.showRoutes = checked
                }
                Switch {
                    text: qsTr("Tracks"); font.pointSize: 12
                    checked: chart.showTracks
                    onToggled: chart.showTracks = checked
                }
                Switch {
                    text: qsTr("Marks"); font.pointSize: 12
                    checked: chart.showWaypoints
                    onToggled: chart.showWaypoints = checked
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#40808080" }

            Label {
                text: qsTr("Routes (") +
                      (routeManagerDialog.rl ? routeManagerDialog.rl.routes.length : 0) + ")"
                font.pointSize: 13; font.bold: true
            }
            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: parent.height * 0.35
                clip: true
                model: routeManagerDialog.rl ? routeManagerDialog.rl.routes : []
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    height: root.touchSize
                    contentItem: RowLayout {
                        Label {
                            text: modelData.name + "  (" + modelData.points + ")"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        ToolButton {
                            text: qsTr("Zoom")
                            onClicked: {
                                chart.fitBounds(modelData.north, modelData.south,
                                                modelData.east, modelData.west)
                                routeManagerDialog.close()
                            }
                        }
                    }
                }
            }

            Label {
                text: qsTr("Marks (") +
                      (routeManagerDialog.rl ? routeManagerDialog.rl.waypoints.length : 0) + ")"
                font.pointSize: 13; font.bold: true
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: routeManagerDialog.rl ? routeManagerDialog.rl.waypoints : []
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    height: root.touchSize
                    contentItem: RowLayout {
                        Label {
                            text: modelData.name
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        ToolButton {
                            text: qsTr("Zoom")
                            onClicked: {
                                chart.fitBounds(modelData.lat, modelData.lat,
                                                modelData.lon, modelData.lon)
                                routeManagerDialog.close()
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Central: world-anchored + display-anchored scene-graph subtrees,
    //     both inside the ChartCanvas QQuickItem.
    ChartCanvas {
        id: chart
        anchors.fill: parent
        // Hand the S-52 engine to the canvas so it scans the chart set's
        // boundaries and streams cell content on demand. `s52` is the
        // context property set in main.cpp.
        s52Engine: s52

        // Tier 3: nav-data HUD (P3.4) -- own-ship SOG/COG/position + AIS
        // count, bound to the ChartCanvas NavStateViewModel.
        Rectangle {
            id: navHud
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 12
            width: hudCol.implicitWidth + 24
            height: hudCol.implicitHeight + 16
            radius: 6
            color: "#cc101418"
            border.color: "#3affffff"

            readonly property var nav: chart.navState

            Column {
                id: hudCol
                anchors.centerIn: parent
                spacing: 2

                Text {
                    text: qsTr("SOG  ") + (navHud.nav ? navHud.nav.sogText : "--")
                    color: "#e0e0e0"; font.pointSize: 13; font.bold: true
                }
                Text {
                    text: qsTr("COG  ") + (navHud.nav ? navHud.nav.cogText : "--")
                    color: "#e0e0e0"; font.pointSize: 13; font.bold: true
                }
                Text {
                    text: navHud.nav ? navHud.nav.positionText : "---"
                    color: "#b0d0ff"; font.pointSize: 11
                }
                Text {
                    text: qsTr("AIS  ") +
                          (navHud.nav ? navHud.nav.aisTargetCount : 0) +
                          qsTr(" targets")
                    color: "#90ee90"; font.pointSize: 11
                }
            }
        }

        // AIS target info popup (P3.9) -- shown when a target is picked
        // (ChartCanvas hit-tests a click against the AisTargetStore).
        Popup {
            id: aisInfo
            readonly property var sel: chart.selectedAis
            visible: sel && sel.valid
            closePolicy: Popup.NoAutoClose
            x: 12
            y: 12
            padding: 14
            background: Rectangle {
                color: "#ee101418"; radius: 8; border.color: "#5affffff"
            }

            ColumnLayout {
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        text: qsTr("AIS target")
                        color: "#90ee90"; font.pointSize: 13; font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        text: "✕"; font.pointSize: 13
                        onClicked: chart.selectedAis.clear()
                    }
                }
                Label {
                    text: (aisInfo.sel && aisInfo.sel.name.length > 0
                           ? aisInfo.sel.name : qsTr("(unnamed)"))
                    color: "#ffffff"; font.pointSize: 14; font.bold: true
                }
                Label {
                    text: qsTr("MMSI  ") + (aisInfo.sel ? aisInfo.sel.mmsi : 0)
                    color: "#c0c0c0"; font.pointSize: 11
                }
                Label {
                    text: aisInfo.sel ? aisInfo.sel.positionText : ""
                    color: "#b0d0ff"; font.pointSize: 11
                }
                Label {
                    text: qsTr("SOG ") + (aisInfo.sel ? aisInfo.sel.sogText : "") +
                          qsTr("   COG ") + (aisInfo.sel ? aisInfo.sel.cogText : "")
                    color: "#e0e0e0"; font.pointSize: 11
                }
            }
        }

        // S-57 object-query popup (P3.9) -- the chart features under a click,
        // bound to ChartCanvas.objectQuery.
        Popup {
            id: objInfo
            readonly property var q: chart.objectQuery
            visible: q && q.valid
            closePolicy: Popup.NoAutoClose
            x: 12
            y: parent.height - height - 12
            width: 360
            height: Math.min(parent.height * 0.5, 360)
            padding: 12
            background: Rectangle {
                color: "#ee101418"; radius: 8; border.color: "#5affffff"
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: objInfo.q ? objInfo.q.className : ""
                        color: "#cfe8ff"; font.pointSize: 14; font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        text: "✕"; font.pointSize: 13
                        onClicked: chart.objectQuery.clear()
                    }
                }
                // Step through the stacked objects (specific -> containing).
                RowLayout {
                    Layout.fillWidth: true
                    visible: objInfo.q && objInfo.q.count > 1
                    ToolButton {
                        text: "‹"; font.pointSize: 15
                        enabled: objInfo.q && objInfo.q.index > 0
                        onClicked: chart.objectQuery.prev()
                    }
                    Label {
                        text: objInfo.q
                              ? (objInfo.q.index + 1) + " / " + objInfo.q.count
                              : ""
                        color: "#a0c0e0"; font.pointSize: 11
                    }
                    ToolButton {
                        text: "›"; font.pointSize: 15
                        enabled: objInfo.q &&
                                 objInfo.q.index < objInfo.q.count - 1
                        onClicked: chart.objectQuery.next()
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: qsTr("up ↑")
                        color: "#607080"; font.pointSize: 10
                    }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    TextArea {
                        readOnly: true
                        wrapMode: TextEdit.Wrap
                        color: "#e0e0e0"
                        font.pointSize: 11
                        font.family: "monospace"
                        text: objInfo.q ? objInfo.q.text : ""
                        background: null
                    }
                }
            }
        }
    }

    // --- Footer: status bar -----------------------------------------------
    footer: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            Label {
                text: s52 ? s52.status : qsTr("S-52: (no engine)")
                color: s52 && s52.ok ? "#1f7a1f" : "#a00000"
                font.pointSize: 11
            }
            Item { Layout.fillWidth: true }
            Label {
                text: chart.demoMode ? qsTr("DEMO") : qsTr("LIVE")
                font.pointSize: 11; font.bold: true
                color: chart.demoMode ? "#b07000" : "#1f7a1f"
            }
        }
    }
}
