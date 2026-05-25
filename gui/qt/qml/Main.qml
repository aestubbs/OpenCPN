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

    // Minimum touch target (logical px) for the on-chart controls.
    readonly property int touchSize: 40

    // Toggle for the on-chart debug/stats overlay (like an FPS counter).
    property bool showDebug: false

    // Native window status bar: cursor lat/lon (left) + chart scale (right).
    footer: ToolBar {
        // macOS bottom bars carry a faint hairline separator along their top.
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Qt.rgba(palette.windowText.r, palette.windowText.g,
                           palette.windowText.b, 0.15)
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            Label {
                text: chart.cursorText.length > 0 ? chart.cursorText
                                                  : qsTr("—")
                font.family: "monospace"
            }
            Item { Layout.fillWidth: true }
            Label { text: chart.scaleText }
        }
    }

    // --- Canvas options: slide-out display panel from the right (mirrors
    //     OpenCPN's MUIBar CanvasOptions). Native right-edge Drawer.
    Drawer {
        id: canvasOptions
        edge: Qt.RightEdge
        width: Math.min(320, root.width * 0.85)
        height: root.height

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
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

    // --- Route & mark manager: a real (non-modal) dialog window (P3.7/C) --
    Window {
        id: routeManagerWindow
        title: qsTr("Routes & marks")
        flags: Qt.Dialog
        width: 460
        height: 560
        color: palette.window

        readonly property var rl: chart.routeList

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
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
                      (routeManagerWindow.rl ? routeManagerWindow.rl.routes.length : 0) + ")"
                font.pointSize: 13; font.bold: true
            }
            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: parent.height * 0.35
                clip: true
                model: routeManagerWindow.rl ? routeManagerWindow.rl.routes : []
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: root.touchSize
                    contentItem: RowLayout {
                        spacing: 4
                        Label {
                            text: (modelData.name.length > 0 ? modelData.name
                                                             : qsTr("(unnamed)"))
                                  + "  (" + modelData.points + ")"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        ToolButton {
                            text: qsTr("Rename")
                            onClicked: {
                                renameDialog.routeIndex = index
                                renameField.text = modelData.name
                                renameDialog.open()
                            }
                        }
                        ToolButton {
                            text: qsTr("Reverse")
                            onClicked: chart.reverseRoute(index)
                        }
                        ToolButton {
                            text: qsTr("Zoom")
                            onClicked: {
                                chart.fitBounds(modelData.north, modelData.south,
                                                modelData.east, modelData.west)
                                routeManagerWindow.close()
                            }
                        }
                        ToolButton {
                            text: "✕"
                            onClicked: chart.deleteRoute(index)
                        }
                    }
                }
            }

            // Rename dialog: prompts for a new name for routeIndex.
            Dialog {
                id: renameDialog
                title: qsTr("Rename route")
                anchors.centerIn: parent
                modal: true
                standardButtons: Dialog.Ok | Dialog.Cancel
                property int routeIndex: -1
                onAccepted: chart.renameRoute(routeIndex, renameField.text)
                TextField {
                    id: renameField
                    implicitWidth: 260
                    selectByMouse: true
                    onAccepted: renameDialog.accept()
                }
            }

            Label {
                text: qsTr("Marks (") +
                      (routeManagerWindow.rl ? routeManagerWindow.rl.waypoints.length : 0) + ")"
                font.pointSize: 13; font.bold: true
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: routeManagerWindow.rl ? routeManagerWindow.rl.waypoints : []
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
                                routeManagerWindow.close()
                            }
                        }
                    }
                }
            }

            DialogButtonBox {
                Layout.fillWidth: true
                standardButtons: DialogButtonBox.Close
                onRejected: routeManagerWindow.close()
            }
        }
    }

    // --- Object query: a real dialog window (P3.9), opened from the chart
    //     right-click "Object query here" menu item -- the wx S57Query flow.
    //     Binds to ChartCanvas.objectQuery; steps through stacked features.
    Window {
        id: objectQueryWindow
        title: qsTr("Object query")
        flags: Qt.Dialog
        width: 420
        height: 480
        color: palette.window

        readonly property var q: chart.objectQuery

        onVisibleChanged: if (!visible) chart.objectQuery.clear()

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 6

            Label {
                text: objectQueryWindow.q ? objectQueryWindow.q.className : ""
                font.pointSize: 15; font.bold: true
            }
            // Step through the stacked objects (specific -> containing).
            RowLayout {
                Layout.fillWidth: true
                visible: objectQueryWindow.q && objectQueryWindow.q.count > 1
                ToolButton {
                    text: "‹"; font.pointSize: 15
                    enabled: objectQueryWindow.q && objectQueryWindow.q.index > 0
                    onClicked: chart.objectQuery.prev()
                }
                Label {
                    text: objectQueryWindow.q
                          ? (objectQueryWindow.q.index + 1) + " / " + objectQueryWindow.q.count
                          : ""
                    font.pointSize: 11
                }
                ToolButton {
                    text: "›"; font.pointSize: 15
                    enabled: objectQueryWindow.q &&
                             objectQueryWindow.q.index < objectQueryWindow.q.count - 1
                    onClicked: chart.objectQuery.next()
                }
                Item { Layout.fillWidth: true }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                TextArea {
                    readOnly: true
                    wrapMode: TextEdit.Wrap
                    font.pointSize: 11
                    font.family: "monospace"
                    text: objectQueryWindow.q ? objectQueryWindow.q.text : ""
                }
            }
            DialogButtonBox {
                Layout.fillWidth: true
                standardButtons: DialogButtonBox.Close
                onRejected: objectQueryWindow.close()
            }
        }
    }

    // --- Options: the full tabbed settings dialog window (mirrors wx
    //     options.cpp). Opened from the toolbar gear. Native TabBar +
    //     StackLayout; the Charts tab carries the wired vector-display
    //     controls, the rest are structured placeholders for now.
    Window {
        id: optionsWindow
        title: qsTr("Options")
        flags: Qt.Dialog
        width: 640
        height: 520
        color: palette.window

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 0
            spacing: 0

            TabBar {
                id: optTabs
                Layout.fillWidth: true
                TabButton { text: qsTr("Display") }
                TabButton { text: qsTr("Charts") }
                TabButton { text: qsTr("Connections") }
                TabButton { text: qsTr("Ships") }
                TabButton { text: qsTr("Plugins") }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: optTabs.currentIndex

                // --- Display (general) ---
                ColumnLayout {
                    Layout.margins: 16
                    spacing: 8
                    Label { text: qsTr("General"); font.pointSize: 14; font.bold: true }
                    Switch {
                        text: qsTr("Auto-follow own ship")
                        checked: chart.followOwnShip
                        onToggled: chart.followOwnShip = checked
                    }
                    Switch {
                        text: qsTr("Demo nav data")
                        checked: chart.demoMode
                        onToggled: chart.demoMode = checked
                    }
                    Item { Layout.fillHeight: true }
                }

                // --- Charts (vector chart display) -- the wired controls ---
                ColumnLayout {
                    Layout.margins: 16
                    spacing: 8
                    Label {
                        text: qsTr("Chart display category")
                        font.pointSize: 14; font.bold: true
                    }
                    ButtonGroup { id: optCatGroup }
                    Repeater {
                        model: [ { label: qsTr("Base"), cat: 0 },
                                 { label: qsTr("Standard"), cat: 1 },
                                 { label: qsTr("All"), cat: 2 } ]
                        delegate: RadioButton {
                            required property var modelData
                            text: modelData.label
                            ButtonGroup.group: optCatGroup
                            checked: chart.displayCategory === modelData.cat
                            onClicked: chart.displayCategory = modelData.cat
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#40808080" }
                    Label { text: qsTr("Detail"); font.pointSize: 14; font.bold: true }
                    Switch {
                        text: qsTr("Soundings")
                        checked: chart.showSoundings
                        onToggled: chart.showSoundings = checked
                    }
                    Switch {
                        text: qsTr("Text labels")
                        checked: chart.showText
                        onToggled: chart.showText = checked
                    }
                    Switch {
                        text: qsTr("Lights")
                        checked: chart.showLights
                        onToggled: chart.showLights = checked
                    }
                    Switch {
                        text: qsTr("Buoys & beacons")
                        checked: chart.showBuoys
                        onToggled: chart.showBuoys = checked
                    }
                    Item { Layout.fillHeight: true }
                }

                // --- Connections (placeholder) ---
                ColumnLayout {
                    Layout.margins: 16
                    spacing: 8
                    Label { text: qsTr("Data connections"); font.pointSize: 14; font.bold: true }
                    Label {
                        text: qsTr("Serial / network connection management is not yet wired into this dialog.")
                        wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7
                    }
                    Item { Layout.fillHeight: true }
                }

                // --- Ships (placeholder) ---
                ColumnLayout {
                    Layout.margins: 16
                    spacing: 8
                    Label { text: qsTr("Own ship & AIS"); font.pointSize: 14; font.bold: true }
                    Label {
                        text: qsTr("Own-ship dimensions, AIS display and CPA/TCPA settings are not yet wired in.")
                        wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7
                    }
                    Item { Layout.fillHeight: true }
                }

                // --- Plugins (placeholder) ---
                ColumnLayout {
                    Layout.margins: 16
                    spacing: 8
                    Label { text: qsTr("Plugins"); font.pointSize: 14; font.bold: true }
                    Label {
                        text: qsTr("Plugin management is not yet available in the Qt build.")
                        wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            DialogButtonBox {
                Layout.fillWidth: true
                Layout.margins: 8
                standardButtons: DialogButtonBox.Close
                onRejected: optionsWindow.close()
            }
        }
    }

    // --- About: a small native dialog window (wx ID_ABOUT). --------------
    Window {
        id: aboutWindow
        title: qsTr("About OpenCPN")
        flags: Qt.Dialog
        width: 420
        height: 260
        color: palette.window

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 10

            Label {
                text: qsTr("OpenCPN")
                font.pointSize: 22; font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: qsTr("Qt / QtQuick prototype")
                opacity: 0.8
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: qsTr("A chart plotter and marine GPS navigation display.\n" +
                           "This build renders S-57/S-52 vector charts through a " +
                           "Qt Quick scene graph.")
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("Running on Qt ") + qtRuntimeVersion
                opacity: 0.7; font.pointSize: 10
                Layout.alignment: Qt.AlignHCenter
            }
            Item { Layout.fillHeight: true }
            DialogButtonBox {
                Layout.fillWidth: true
                standardButtons: DialogButtonBox.Close
                onRejected: aboutWindow.close()
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

        // Compass rose (mirrors wx's ocpnCompass overlay). The chart is
        // north-up, so the rose is fixed N-up; the red needle shows own-ship
        // COG. Top-right corner.
        Rectangle {
            id: compass
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 12
            width: 72; height: 72; radius: width / 2
            color: "#cc101418"
            border.color: "#3affffff"

            readonly property var nav: chart.navState

            Canvas {
                id: rose
                anchors.fill: parent
                anchors.margins: 6
                // Repaint when COG changes.
                property real cog: compass.nav && compass.nav.ownShipValid
                                   ? compass.nav.cog : -1
                onCogChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    var cx = width / 2, cy = height / 2
                    var r = Math.min(cx, cy) - 2
                    // Outer ring.
                    ctx.strokeStyle = "#80c0d0e0"; ctx.lineWidth = 1.5
                    ctx.beginPath(); ctx.arc(cx, cy, r, 0, 2 * Math.PI); ctx.stroke()
                    // North marker (top) -- a small triangle + "N".
                    ctx.fillStyle = "#e0e0e0"
                    ctx.beginPath()
                    ctx.moveTo(cx, cy - r); ctx.lineTo(cx - 4, cy - r + 8)
                    ctx.lineTo(cx + 4, cy - r + 8); ctx.closePath(); ctx.fill()
                    // COG needle (red), 0 deg = up, clockwise.
                    if (cog >= 0) {
                        var a = (cog - 90) * Math.PI / 180
                        ctx.strokeStyle = "#ff5050"; ctx.lineWidth = 2.5
                        ctx.beginPath(); ctx.moveTo(cx, cy)
                        ctx.lineTo(cx + r * 0.8 * Math.cos(a),
                                   cy + r * 0.8 * Math.sin(a))
                        ctx.stroke()
                    }
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 7
                text: "N"; color: "#e0e0e0"; font.pointSize: 9; font.bold: true
            }
        }

        // Tier 3: nav-data HUD (P3.4) -- own-ship SOG/COG/position + AIS
        // count, bound to the ChartCanvas NavStateViewModel.
        Rectangle {
            id: navHud
            anchors.top: compass.bottom
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

        // Chart bar / "Piano" (P3.8) -- one key per ENC cell covering the
        // view, coarse->fine, mirroring wx's chart-selector bar. Each key
        // shows the cell name and is tinted by usage band; keys for cells
        // currently in the quilt are outlined. Clicking a key HIGHLIGHTS that
        // cell's coverage on the chart (toggle) -- it does not move the view.
        Rectangle {
            id: chartBar
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 10
            height: 26
            width: Math.min(barRow.implicitWidth + 8, chart.width - 24)
            visible: barRow.count > 0
            radius: 4
            color: "#cc101418"
            border.color: "#3affffff"
            clip: true

            property var cells: chart.chartBarCells()
            Connections {
                target: chart
                function onChartCoverageChanged() { chartBar.cells = chart.chartBarCells() }
            }
            // Usage-band tint: overview->berthing, coarse blue -> warm harbour.
            function bandColor(b) {
                switch (b) {
                case 1: return "#5b7fb4"
                case 2: return "#4f9bb0"
                case 3: return "#4faf7a"
                case 4: return "#9bAf4f"
                case 5: return "#c08a3e"
                case 6: return "#c0623e"
                default: return "#808890"
                }
            }

            Row {
                id: barRow
                anchors.centerIn: parent
                spacing: 2
                property int count: chartBar.cells ? chartBar.cells.length : 0
                Repeater {
                    model: chartBar.cells
                    delegate: Rectangle {
                        required property var modelData
                        implicitWidth: Math.max(40, keyLabel.implicitWidth + 12)
                        height: 20; radius: 3
                        color: chartBar.bandColor(modelData.band)
                        // Cells currently in the quilt (drawn) get a bright
                        // outline; merely-available cells a faint one.
                        border.width: modelData.displayed ? 2 : 1
                        border.color: modelData.displayed ? "#e8f0ff" : "#50000000"
                        Text {
                            id: keyLabel
                            anchors.centerIn: parent
                            text: modelData.name
                            color: "#ffffff"
                            // Monospaced so confusable glyphs in NOAA cell IDs
                            // (e.g. US4CA11M vs US4CA1IM -- digit-1 vs cap-I)
                            // are distinguishable, as on the wx chart bar.
                            font.family: "monospace"; font.pointSize: 9
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            // Hover = show coverage outline (wx piano rollover);
                            // click = autoscale to the chart (wx piano click).
                            onEntered: chart.highlightChartCell(modelData.name)
                            onExited: chart.highlightChartCell("")
                            onClicked: chart.selectChart(modelData.name)
                            ToolTip.visible: containsMouse
                            ToolTip.text: modelData.name + "  (1:" + modelData.scale +
                                          ", band " + modelData.band + ")"
                        }
                    }
                }
            }
        }

        // Debug / stats overlay (toggle via the menu, like an FPS counter).
        // Off by default so it never obscures the chart bar.
        Rectangle {
            visible: root.showDebug
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 10
            width: dbgText.implicitWidth + 16
            height: dbgText.implicitHeight + 10
            radius: 4
            color: "#cc101418"
            border.color: "#3affffff"
            Text {
                id: dbgText
                anchors.centerIn: parent
                text: (s52 ? s52.status : qsTr("S-52: (no engine)")) +
                      (chart.demoMode ? qsTr("   [DEMO]") : qsTr("   [LIVE]"))
                color: s52 && s52.ok ? "#a8e0a8" : "#e0a0a0"
                font.pointSize: 10
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

        // Right-click context menu (wx canvas-menu equivalent). Opened at the
        // click point via the ChartCanvas.contextMenuRequested signal.
        Menu {
            id: chartContextMenu
            MenuItem {
                text: qsTr("Object query here")
                onTriggered: {
                    chart.queryObjectsHere()
                    objectQueryWindow.show(); objectQueryWindow.raise()
                }
            }
            MenuItem {
                text: qsTr("Center view here")
                onTriggered: chart.centerViewHere()
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Create route")
                onTriggered: chart.routeBuildMode = true
            }
            // wx canvas-menu items not yet wired (kept for layout parity).
            MenuItem { text: qsTr("Drop mark here"); enabled: false }
            MenuItem { text: qsTr("Measure"); enabled: false }
        }
        Connections {
            target: chart
            function onContextMenuRequested(x, y) {
                chartContextMenu.popup(x, y)
            }
        }

        // Route-node context menu (right-click a node of the selected route).
        Menu {
            id: routeNodeMenu
            MenuItem {
                text: qsTr("Delete point")
                onTriggered: chart.deleteRoutePointAtMenu()
            }
            MenuItem {
                text: qsTr("Delete route")
                onTriggered: chart.deleteSelectedRoute()
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Finish editing")
                onTriggered: chart.clearRouteSelection()
            }
        }
        Connections {
            target: chart
            function onRouteNodeMenuRequested(x, y) {
                routeNodeMenu.popup(x, y)
            }
        }

        // Editing hint banner: shown while a route is selected for editing.
        Rectangle {
            visible: chart.selectedRoute >= 0
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 10
            width: editHint.implicitWidth + 20
            height: editHint.implicitHeight + 12
            radius: 4
            color: "#cc1a1e10"
            border.color: "#80ffc83c"
            Text {
                id: editHint
                anchors.centerIn: parent
                text: qsTr("Editing route — drag nodes · click line to add · " +
                           "right-click node to delete · click water to finish")
                color: "#ffe0a0"; font.pointSize: 10
            }
        }
    }

    // --- Floating master toolbar (mirrors OpenCPN's single vertical wx
    //     floating toolbar) ----------------------------------------------
    // One frameless native Pane floating over the full-bleed chart, oriented
    // vertically on the left edge and draggable anywhere within the window.
    // Carries the full wx master-toolbar tool set in wx order; tools we have
    // not wired up yet are present but inert (tooltip only, no action) so the
    // layout matches wx and the actions can be connected incrementally.
    Pane {
        id: floatToolbar
        x: 16
        y: 16
        padding: 4

        // Reusable vertical tool factory so every button is sized / tooltipped
        // identically. Inert tools just omit an onClicked handler.
        component Tool: ToolButton {
            font.pointSize: 16
            implicitWidth: root.touchSize
            implicitHeight: root.touchSize
            Layout.alignment: Qt.AlignHCenter
            ToolTip.visible: hovered && ToolTip.text.length > 0
            ToolTip.delay: 400
        }

        ColumnLayout {
            spacing: 2

            // 1. Menu (wx "Hide Toolbar" master button).
            Tool {
                text: "☰"; ToolTip.text: qsTr("Menu")
                onClicked: mainMenu.popup(floatToolbar, floatToolbar.width, 0)
            }
            // 2. Options -> full tabbed settings dialog (wx ID_SETTINGS).
            Tool {
                text: "⚙"; ToolTip.text: qsTr("Options")
                onClicked: { optionsWindow.show(); optionsWindow.raise() }
            }

            ToolSeparator { Layout.fillWidth: true }

            // Zoom / fit / follow -- our wired navigation controls (wx keeps
            // these on the per-canvas MUIBar; consolidated here for now).
            Tool {
                text: "+"; font.pointSize: 19; ToolTip.text: qsTr("Zoom in")
                onClicked: chart.zoomIn()
            }
            Tool {
                text: "−"; font.pointSize: 19; ToolTip.text: qsTr("Zoom out")
                onClicked: chart.zoomOut()
            }
            Tool {
                text: "⤢"; font.pointSize: 14; ToolTip.text: qsTr("Fit / zoom to world")
                onClicked: chart.fitWorld()
            }
            Tool {
                text: "⊙"; ToolTip.text: qsTr("Auto-follow own ship")
                checkable: true
                checked: chart.followOwnShip
                onClicked: chart.followOwnShip = checked
            }

            ToolSeparator { Layout.fillWidth: true }

            // 3. Create Route (wx ID_MENU_ROUTE_NEW).
            Tool {
                text: "✚"; checkable: true
                checked: chart.routeBuildMode
                ToolTip.text: qsTr("Create route  (left-click adds points, right-click finishes)")
                onClicked: chart.routeBuildMode = checked
            }
            // 4. Route & Mark Manager -> the dialog window.
            Tool {
                text: "▤"; ToolTip.text: qsTr("Route && mark manager")
                onClicked: { routeManagerWindow.show(); routeManagerWindow.raise() }
            }
            // 5. Enable Tracking (wx ID_TRACK).
            Tool {
                text: "⊚"; checkable: true
                checked: chart.trackRecording
                ToolTip.text: qsTr("Record own-ship track")
                onClicked: chart.trackRecording = checked
            }
            // 6. Change Color Scheme (wx ID_COLSCHEME): cycle day/dusk/night.
            Tool {
                text: "◑"
                ToolTip.text: [qsTr("Color scheme: Day"),
                               qsTr("Color scheme: Dusk"),
                               qsTr("Color scheme: Night")][chart.colorScheme]
                onClicked: chart.colorScheme = (chart.colorScheme + 1) % 3
            }
            // 7. Print Chart (wx ID_PRINT) -- not wired yet.
            Tool {
                text: "⎙"; ToolTip.text: qsTr("Print chart (not yet implemented)")
            }
            // 8. About OpenCPN (wx ID_ABOUT).
            Tool {
                text: "ⓘ"; ToolTip.text: qsTr("About OpenCPN")
                onClicked: { aboutWindow.show(); aboutWindow.raise() }
            }
            // 9. Drop MOB Marker (wx ID_MOB) -- not wired yet.
            Tool {
                text: "⚓"; ToolTip.text: qsTr("Drop MOB marker (not yet implemented)")
            }

            ToolSeparator { Layout.fillWidth: true }

            // Live chart-scale readout (was on the MUIBar).
            Label {
                text: chart.scaleText
                font.pointSize: 9
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
            }
        }

        // Drag the whole toolbar; clamp within the window.
        DragHandler {
            target: floatToolbar
            xAxis.minimum: 0
            xAxis.maximum: root.width - floatToolbar.width
            yAxis.minimum: 0
            yAxis.maximum: root.height - floatToolbar.height
        }
    }

    Menu {
        id: mainMenu
        MenuItem {
            text: qsTr("Quick display…")
            onTriggered: canvasOptions.open()
        }
        MenuItem {
            text: qsTr("Options…")
            onTriggered: { optionsWindow.show(); optionsWindow.raise() }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Show debug info"); checkable: true
            checked: root.showDebug
            onTriggered: root.showDebug = checked
        }
        MenuItem {
            text: qsTr("Demo mode"); checkable: true
            checked: chart.demoMode
            onTriggered: chart.demoMode = checked
        }
        MenuItem {
            text: qsTr("Drop demo here")
            onTriggered: chart.dropDemoHere()
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Quit"); onTriggered: Qt.quit() }
    }

    // Colour-scheme dim overlay (#30): tints the whole window for dusk/night,
    // mirroring how OpenCPN dims the display. Plain item, input-transparent
    // (enabled:false) so it never intercepts chart/toolbar interaction.
    Rectangle {
        anchors.fill: parent
        enabled: false
        visible: chart.colorScheme !== 0
        color: chart.colorScheme === 2 ? "#66200000"   // night: dark red wash
                                       : "#400a1432"   // dusk: dark blue wash
    }
}
