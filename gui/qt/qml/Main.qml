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
import QtQuick.Dialogs

import opencpn.qt

ApplicationWindow {
    id: root
    visible: true
    width: 1024
    height: 720
    title: qsTr("OpenCPN-NG")

    // Minimum touch target (logical px) for the on-chart controls. Scaled by
    // the UI scale factor (Options > User Interface): -5..+5 -> ~0.4x..~1.6x.
    readonly property int touchSize: Math.round(40 * (1 + 0.12 * UIConfig.guiScaleFactor))

    // Toggle for the on-chart debug/stats overlay (like an FPS counter).
    property bool showDebug: false

    // Expandable vessel-data HUD panel on the right edge (own-ship gauges).
    property bool hudExpanded: false

    // wx "Hide Toolbar": collapse the floating master toolbar to its toggle.
    property bool toolbarCollapsed: false

    // Native window status bar -- mirrors the wx 5-field bar: ship position
    // (+ NMEA heartbeat tick), SOG/COG, cursor lat/lon, cursor bearing/range
    // from own ship, and chart scale. Proportional widths 6:5:5:6:4 as in wx.
    footer: ToolBar {
        id: statusBar
        visible: UIConfig.showStatusBar  // Options > User Interface

        // NMEA heartbeat: advance a spinner glyph on each nav update, so a
        // live feed is visibly "ticking" (wx STAT_FIELD_TICK).
        readonly property var spinner: ["⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧"]
        property int tick: 0
        readonly property var nav: chart.navState
        Connections {
            target: chart.navState
            function onChanged() { statusBar.tick = (statusBar.tick + 1) % 8 }
        }

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
            spacing: 8

            component Sep: Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                Layout.topMargin: 4; Layout.bottomMargin: 4
                color: Qt.rgba(palette.windowText.r, palette.windowText.g,
                               palette.windowText.b, 0.15)
            }
            component Field: Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.family: "monospace"
            }

            // 0: Ship position + heartbeat tick.
            Field {
                Layout.preferredWidth: 6
                text: (statusBar.nav && statusBar.nav.ownShipValid
                       ? statusBar.spinner[statusBar.tick] + " " : "  ") +
                      qsTr("Ship ") +
                      (statusBar.nav ? statusBar.nav.positionText : "---")
            }
            Sep {}
            // 1: SOG / COG.
            Field {
                Layout.preferredWidth: 5
                text: qsTr("SOG ") + (statusBar.nav ? statusBar.nav.sogText : "--") +
                      qsTr("  COG ") + (statusBar.nav ? statusBar.nav.cogText : "--")
            }
            Sep {}
            // 2: Cursor lat/lon.
            Field {
                Layout.preferredWidth: 5
                text: chart.cursorText.length > 0 ? chart.cursorText : qsTr("—")
            }
            Sep {}
            // 3: Cursor bearing/range from own ship.
            Field {
                Layout.preferredWidth: 6
                text: chart.cursorBrgRngText
            }
            Sep {}
            // 4: Chart scale -- click to type a scale. Free-form like wx's
            //    Set-Scale (ChartCanvas clamps to 1:1,000 .. 1:3,000,000).
            Field {
                id: scaleField
                Layout.preferredWidth: 4
                horizontalAlignment: Text.AlignRight
                text: chart.scaleText
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: scaleDialog.openWithScale(
                                  chart.scaleText.replace(/^.*:/, ""))
                }
            }
        }
    }

    // Scale entry (ScaleDialog.qml, P3.17): clicking the status-bar scale
    // opens this to type a free-form 1:N value.
    ScaleDialog { id: scaleDialog }

    // --- Canvas options (CanvasOptionsDrawer.qml, P3.17): right-edge quick
    //     display panel (mirrors OpenCPN's MUIBar CanvasOptions); shares
    //     VectorDetailList.qml with Options > Charts > Vector Display.
    CanvasOptionsDrawer {
        id: canvasOptions
        appWindow: root
    }

    // --- Route & mark manager (RouteManagerDrawer.qml, P3.7/P3.17): left-edge
    //     drawer of route / mark / track tiles; non-modal so the chart stays
    //     live behind it.
    RouteManagerDrawer {
        id: routeDrawer
        appWindow: root
        onRouteDetailsRequested: (idx, nm) => routeDetailsDialog.openFor(idx, nm)
        onEditMarkRequested: (g, nm, cm, ic) =>
                                 markEditor.openForEdit(g, nm, cm, ic)
    }

    // --- Mark editor (MarkEditorDialog.qml, P3.7/P3.17): one dialog for
    //     "New mark" (chart right-click) and "Edit mark" (drawer tile Edit).
    MarkEditorDialog { id: markEditor }

    // --- Route details (RouteDetailsWindow.qml, P3.17): route name + the
    //     per-route mark icon. Opened from the routes drawer ⋯ menu.
    RouteDetailsWindow { id: routeDetailsDialog }

    // --- Object query (ObjectQueryWindow.qml, P3.9/P3.17): the wx S57Query
    //     flow; binds ChartCanvas.objectQuery, steps through stacked features.
    ObjectQueryWindow { id: objectQueryWindow }

    // --- Options: the tabbed settings window (OptionsWindow.qml, P3.17).
    //     Modeless dialog-flagged window; opened from the master toolbar.
    OptionsWindow {
        id: optionsWindow
        appWindow: root
    }

    // --- About (AboutWindow.qml, P3.17): mirrors the wx About dialog. ---
    AboutWindow { id: aboutWindow }

    // --- Data Monitor (DataMonitorWindow.qml, P3.17): live decoded NMEA/N2K
    //     stream with pause + per-source filter.
    DataMonitorWindow { id: dataMonitorWindow }

    // --- Central: world-anchored + display-anchored scene-graph subtrees,
    //     both inside the ChartCanvas QQuickItem.
    ChartCanvas {
        id: chart
        clip: true   // never rasterise chart geometry into the tide drawer below
        anchors.left: parent.left
        anchors.top: parent.top
        // Bottom follows the tide graph drawer (which sits on the time bar);
        // the chart shrinks for the bar, then further as the drawer opens.
        anchors.bottom: tideDrawer.top
        // Right edge follows the HUD panel so opening it shrinks the chart.
        anchors.right: hudPanel.left
        // Hand the S-52 engine to the canvas so it scans the chart set's
        // boundaries and streams cell content on demand. `s52` is the
        // context property set in main.cpp.
        s52Engine: s52

        // --- AIS CPA/TCPA danger alert (P3.15). The C++ AlertEngine raises
        //     the banner and requests the user's AIS alert sound; Acknowledge
        //     silences it for AisConfig.ackTimeoutMin.
        Connections {
            target: chart.alerts
            function onSoundRequested(file) { SoundPlayer.play(file) }
        }
        Rectangle {
            id: alertBanner
            z: 100
            visible: chart.alerts.alertActive
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 12
            width: Math.min(parent.width - 24, alertRow.implicitWidth + 28)
            height: alertRow.implicitHeight + 14
            radius: 6
            color: "#e6c81e1e"            // alert red, slightly translucent
            border.color: "#ffd6d6"; border.width: 1
            // Gentle pulse so the alert draws the eye while active.
            SequentialAnimation on opacity {
                running: alertBanner.visible
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.62; duration: 700 }
                NumberAnimation { from: 0.62; to: 1.0; duration: 700 }
            }
            RowLayout {
                id: alertRow
                anchors.centerIn: parent
                spacing: 14
                Text {
                    text: "⚠  " + chart.alerts.alertText
                    color: "white"
                    font.pixelSize: 15
                    font.bold: true
                }
                Button {
                    text: qsTr("Acknowledge")
                    onClicked: chart.alerts.acknowledge()
                }
            }
        }

        // Route edit-mode banner (P3.7): shown while a selected route is
        // editable. Top-left so it clears the centred alert/overscale banners.
        Rectangle {
            visible: chart.routeEditMode
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 12
            z: 100
            width: editRow.implicitWidth + 24
            height: editRow.implicitHeight + 14
            radius: 6
            color: "#e6244062"
            border.color: "#7fb0d0ff"; border.width: 1
            RowLayout {
                id: editRow
                anchors.centerIn: parent
                spacing: 12
                Label {
                    text: qsTr("Editing route — drag a node, click a leg to add, right-click a node to remove")
                    color: "white"; font.pointSize: 11
                }
                Button {
                    text: qsTr("Done")
                    onClicked: chart.routeEditMode = false
                }
            }
        }

        // --- Test ship (P3.16): cursor-key steering. A transparent overlay
        //     that holds keyboard focus while the sim is active so the arrow
        //     keys steer it (Left/Right course, Up/Down speed, Space run). It
        //     has no MouseArea, so chart pan/zoom is unaffected.
        Item {
            id: simKeyHandler
            anchors.fill: parent
            z: 90
            focus: chart.simShip.active
            Keys.onPressed: function(e) {
                if (!chart.simShip.active) { e.accepted = false; return }
                switch (e.key) {
                case Qt.Key_Left:  chart.simShip.steer(-5);    e.accepted = true; break
                case Qt.Key_Right: chart.simShip.steer(5);     e.accepted = true; break
                case Qt.Key_Up:    chart.simShip.throttle(1);  e.accepted = true; break
                case Qt.Key_Down:  chart.simShip.throttle(-1); e.accepted = true; break
                case Qt.Key_Space: chart.simShip.toggleRun();  e.accepted = true; break
                default: e.accepted = false
                }
            }
        }

        // --- Test ship control panel (P3.16): course/speed + run state, shown
        //     while the test ship is active. Top-left.
        Rectangle {
            id: simPanel
            visible: chart.simShip.active
            z: 95
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 12
            width: simCol.implicitWidth + 24
            height: simCol.implicitHeight + 16
            radius: 6
            color: "#cc101418"
            border.color: chart.simShip.running ? "#ff5a28" : "#3affffff"
            border.width: 1
            Column {
                id: simCol
                anchors.centerIn: parent
                spacing: 3
                Text {
                    text: "▣ " + qsTr("Test ship") +
                          (chart.simShip.running ? "  ▸ " + qsTr("under way")
                                                 : "  ❚❚ " + qsTr("stopped"))
                    color: chart.simShip.running ? "#ff9a6a" : "#e0e0e0"
                    font.pointSize: 12; font.bold: true
                }
                Text {
                    text: qsTr("HDG ") + chart.simShip.course.toFixed(0) + "°    " +
                          qsTr("SPD ") + chart.simShip.speed.toFixed(1) + qsTr(" kn")
                    color: "#e0e0e0"; font.pointSize: 11
                }
                Text {
                    text: qsTr("← → course · ↑ ↓ speed · space run")
                    color: "#90a0b0"; font.pointSize: 9
                }
                Row {
                    spacing: 6; topPadding: 2
                    Button {
                        text: chart.simShip.running ? qsTr("Stop") : qsTr("Go")
                        font.pointSize: 10
                        onClicked: {
                            chart.simShip.toggleRun()
                            simKeyHandler.forceActiveFocus()
                        }
                    }
                    Button {
                        text: qsTr("Remove")
                        font.pointSize: 10
                        onClicked: chart.simShip.setActive(false)
                    }
                }
            }
        }

        // Compass rose (mirrors wx's ocpnCompass overlay). The chart is
        // north-up, so the rose is fixed N-up; the red needle shows own-ship
        // COG. Top-right corner.
        Rectangle {
            id: compass
            // Hidden when the HUD panel supersedes it, or by the Display
            // option (wx "Show compass window").
            visible: !app.hudExpanded && DisplayConfig.showCompass
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 12
            width: 72; height: 72; radius: width / 2
            color: "#cc101418"
            border.color: "#3affffff"
            // The compass card rotates with the chart so "N" always indicates
            // true north on screen (Course-Up / Head-Up). North-Up = 0.
            rotation: chart.chartRotationDeg
            Behavior on rotation { RotationAnimation { duration: 120; direction: RotationAnimation.Shortest } }

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
            // Active-orientation badge (N / COG / HDG). Counter-rotated so it
            // stays upright while the card turns.
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 6
                rotation: -compass.rotation
                text: ["N", "COG", "HDG"][DisplayConfig.navMode]
                color: DisplayConfig.navMode === 0 ? "#e0e0e0" : "#90c8ff"
                font.pointSize: 9; font.bold: true
            }

            // Click the rose to cycle North-Up -> Course-Up -> Head-Up.
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: DisplayConfig.navMode = (DisplayConfig.navMode + 1) % 3
                ToolTip.visible: containsMouse
                ToolTip.text: [qsTr("North-Up (click to change)"),
                               qsTr("Course-Up (click to change)"),
                               qsTr("Head-Up (click to change)")][DisplayConfig.navMode]
            }
        }

        // Tier 3: nav-data HUD (P3.4) -- own-ship SOG/COG/position + AIS
        // count, bound to the ChartCanvas NavStateViewModel.
        Rectangle {
            id: navHud
            visible: !app.hudExpanded  // the HUD panel supersedes it
            anchors.top: compass.bottom
            anchors.right: parent.right
            anchors.margins: 12
            width: hudCol.implicitWidth + 24
            height: hudCol.implicitHeight + 16
            radius: 6
            color: "#cc101418"
            border.color: "#3affffff"

            readonly property var nav: chart.navState
            // Active-route following solution (P3.16); null-safe via `active`.
            readonly property var rf: chart.routeFollower

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

                // --- Active route (P3.16): the live nav solution, shown only
                //     while a route is being followed. ---
                readonly property bool following: navHud.rf && navHud.rf.active
                Rectangle {
                    visible: hudCol.following
                    width: hudCol.width; height: 1
                    color: "#30ffffff"
                }
                Text {
                    visible: hudCol.following
                    text: "▸ " + (navHud.rf ? navHud.rf.routeName : "") +
                          "   " + (navHud.rf ? navHud.rf.legText : "")
                    color: "#ff9a6a"; font.pointSize: 12; font.bold: true
                }
                Text {
                    visible: hudCol.following
                    text: "→ " + (navHud.rf ? navHud.rf.toWaypoint : "")
                    color: "#e0e0e0"; font.pointSize: 11
                }
                Text {
                    visible: hudCol.following
                    text: qsTr("BRG ") + (navHud.rf ? navHud.rf.btwText : "") +
                          qsTr("   DTW ") + (navHud.rf ? navHud.rf.dtwText : "")
                    color: "#e0e0e0"; font.pointSize: 11
                }
                Text {
                    visible: hudCol.following
                    text: qsTr("XTE ") + (navHud.rf ? navHud.rf.xteText : "")
                    color: "#e0e0e0"; font.pointSize: 11
                }
                Text {
                    visible: hudCol.following
                    text: qsTr("VMG ") + (navHud.rf ? navHud.rf.vmgText : "") +
                          qsTr("   ETA ") + (navHud.rf ? navHud.rf.etaText : "")
                    color: "#e0e0e0"; font.pointSize: 11
                }
                Row {
                    visible: hudCol.following
                    spacing: 6
                    topPadding: 2
                    Button {
                        text: qsTr("Skip ▸")
                        font.pointSize: 10
                        onClicked: chart.skipWaypoint()
                    }
                    Button {
                        text: qsTr("Stop ■")
                        font.pointSize: 10
                        onClicked: chart.deactivateRoute()
                    }
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
            visible: barRow.count > 0 && UIConfig.showChartBar  // Options > UI
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

        // --- Scale bar (mirrors wx ScaleBarDraw): bottom-left, a "nice" round
        //     distance for the current zoom + label, recomputed on pan/zoom and
        //     when the distance unit changes. Styled like the other HUD pills.
        Rectangle {
            id: scaleBar
            property var sb: chart.scaleBar()
            Connections {
                target: chart
                function onViewChanged() { scaleBar.sb = chart.scaleBar() }
            }
            Connections {
                target: DisplayConfig
                function onChanged() { scaleBar.sb = chart.scaleBar() }
            }
            // scaleBar() yields an empty map until the viewport is ready.
            readonly property real barLen: (sb && sb.length) ? sb.length : 0
            readonly property string barText: (sb && sb.label) ? sb.label : ""
            visible: barLen > 6
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 12
            width: barLen + 16
            height: barCol.implicitHeight + 12
            radius: 4
            color: "#cc101418"
            border.color: "#3affffff"

            Column {
                id: barCol
                anchors.centerIn: parent
                spacing: 3
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: scaleBar.barText
                    color: "#e8f0ff"; font.pointSize: 10; font.bold: true
                }
                // The bar: a baseline with end ticks, exactly `length` px wide.
                Item {
                    width: scaleBar.barLen
                    height: 8
                    Rectangle {  // baseline
                        anchors.bottom: parent.bottom
                        width: parent.width; height: 2; color: "#e8f0ff"
                    }
                    Rectangle {  // left tick
                        anchors.left: parent.left; anchors.bottom: parent.bottom
                        width: 2; height: 8; color: "#e8f0ff"
                    }
                    Rectangle {  // right tick
                        anchors.right: parent.right; anchors.bottom: parent.bottom
                        width: 2; height: 8; color: "#e8f0ff"
                    }
                }
            }
        }

        // --- MUIBar: per-canvas view controls bottom-right (MuiBar.qml, P3.17).
        //     Zoom / fit, the Follow / jump-to-ship split button, and the
        //     canvas-options menu. Tides + anchor now live on the master toolbar.
        MuiBar {
            // Initial position bottom-right; the binding holds until the user
            // drags it (a drag breaks the binding), then it stays where put.
            x: parent.width - width - 12
            y: parent.height - height - 12
            onCanvasOptionsRequested: canvasOptions.open()
        }

        // Anchor-watch control (P3.15): drop the watch at the current fix, set
        // the radius, or raise it. The circle is drawn by AnchorWatchLayer and
        // the alarm by the AlertEngine.
        Popup {
            id: anchorPopup
            parent: chart
            x: parent.width - width - 12
            y: parent.height - height - 56
            padding: 12
            modal: false
            ColumnLayout {
                spacing: 8
                Label {
                    text: chart.alerts.anchorBreach
                              ? qsTr("⚓ DRAGGING — outside watch circle")
                              : chart.alerts.anchorSet
                                  ? qsTr("⚓ Anchor watch armed")
                                  : qsTr("Anchor watch off")
                    color: chart.alerts.anchorBreach ? "#e02020" : palette.windowText
                    font.bold: chart.alerts.anchorBreach
                }
                RowLayout {
                    spacing: 6
                    Label { text: qsTr("Watch radius") }
                    SpinBox {
                        from: 5; to: 1000; stepSize: 5
                        value: Math.round(chart.alerts.anchorRadiusM)
                        onValueModified: chart.alerts.setAnchorRadiusM(value)
                    }
                    Label { text: qsTr("m") }
                }
                RowLayout {
                    spacing: 6
                    Button {
                        text: qsTr("Drop anchor")
                        enabled: !chart.alerts.anchorSet
                        onClicked: chart.alerts.dropAnchor()
                    }
                    Button {
                        text: qsTr("Raise anchor")
                        enabled: chart.alerts.anchorSet
                        onClicked: chart.alerts.raiseAnchor()
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
                text: (chart.perfText.length ? chart.perfText + "    " : "") +
                      (s52 ? s52.status : qsTr("S-52: (no engine)")) +
                      (chart.demoMode ? qsTr("   [DEMO]") : qsTr("   [LIVE]"))
                color: s52 && s52.ok ? "#a8e0a8" : "#e0a0a0"
                font.pointSize: 10
            }
        }

        // Over-scale warning (S-52): the displayed chart is magnified beyond
        // its compilation scale, so detail is stretched and not survey-accurate.
        // Shown top-centre (under any debug pill) when the factor exceeds ~2x.
        Rectangle {
            visible: chart.overscaleFactor > 2.0
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: root.showDebug ? 44 : 10
            width: oscText.implicitWidth + 18
            height: oscText.implicitHeight + 10
            radius: 4
            color: "#cc6a1010"           // muted dark red
            border.color: "#ffcf4040"
            Text {
                id: oscText
                anchors.centerIn: parent
                text: qsTr("⚠ OVERSCALE ×%1").arg(
                          Math.round(chart.overscaleFactor))
                color: "#ffd0d0"
                font.pointSize: 11
                font.bold: true
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
            readonly property bool danger: aisInfo.sel && aisInfo.sel.dangerous
            background: Rectangle {
                color: "#ee101418"; radius: 8
                // Red border + glow when the target is a CPA/TCPA threat.
                border.color: aisInfo.danger ? "#ff3b30" : "#5affffff"
                border.width: aisInfo.danger ? 2 : 1
            }

            ColumnLayout {
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        text: aisInfo.danger ? qsTr("⚠ AIS — CPA ALERT")
                                             : qsTr("AIS target")
                        color: aisInfo.danger ? "#ff453a" : "#90ee90"
                        font.pointSize: 13; font.bold: true
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
                Label {
                    text: qsTr("RNG ") + (aisInfo.sel ? aisInfo.sel.rangeText : "") +
                          qsTr("   BRG ") + (aisInfo.sel ? aisInfo.sel.bearingText : "")
                    color: "#e0e0e0"; font.pointSize: 11
                }
                Label {
                    text: qsTr("CPA ") + (aisInfo.sel ? aisInfo.sel.cpaText : "") +
                          qsTr("   TCPA ") + (aisInfo.sel ? aisInfo.sel.tcpaText : "")
                    color: aisInfo.danger ? "#ff8c80" : "#e0e0e0"
                    font.pointSize: 11; font.bold: aisInfo.danger
                }
                // Draw this vessel's recorded trail (last 5x the predictor
                // reach) from the SQLite history. Per-vessel; off by default.
                CheckBox {
                    text: qsTr("Show trail")
                    font.pointSize: 10
                    checked: aisInfo.sel ? chart.aisTrailEnabled(aisInfo.sel.mmsi)
                                         : false
                    onToggled: if (aisInfo.sel)
                                   chart.setAisTrail(aisInfo.sel.mmsi, checked)
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
            MenuItem {
                text: qsTr("Drop mark here")
                onTriggered: markEditor.openNew()  // dialog uses the ctx point
            }
            MenuSeparator {}
            MenuItem {
                // Test ship (P3.16): drop a synthetic GPS here and grab the
                // keyboard so the cursor keys steer it straight away.
                text: chart.simShip.active ? qsTr("Move test ship here")
                                           : qsTr("Place test ship here")
                onTriggered: {
                    chart.placeSimShipHere()
                    simKeyHandler.forceActiveFocus()
                }
            }
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

        // (The old selection-bound "Editing route…" hint was removed: selection
        // and edit are now distinct -- the edit banner above is gated on
        // chart.routeEditMode, P3.7.)
    }

    // --- Tide/current graph drawer (P3.14 F): the graph grows UP out of the
    //     top of the time bar, so the bar's hour ticks ARE the graph's x-axis
    //     (no gap). Opens when a tide/current station is clicked; the y-axis
    //     sits at the window's left edge. Drag the graph (or the bar) to pan.
    Item {
        id: tideDrawer
        anchors.left: parent.left
        anchors.right: hudPanel.left
        anchors.bottom: tideBar.top          // grows up from the bar's top edge
        readonly property bool open: DisplayConfig.showTides
            && chart.tideGraph && chart.tideGraph.valid
        height: open ? 170 : 0
        clip: true
        Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

        Rectangle {  // one continuous panel with the bar -- no borders/seams
            anchors.fill: parent
            color: "#0b1118"
        }

        Canvas {  // the tide/current curve, sharing the bar's plot x-range
            id: graphCanvas
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d"); ctx.reset()
                var tg = chart.tideGraph
                if (!tg || !tg.valid) return
                var pl = tideBar.plotLeft, pw = tideBar.plotWidth
                if (pw <= 1) return
                var topM = 22, botM = 2   // header band on top; curve meets the bar at the bottom
                var plotH = height - topM - botM
                if (plotH < 10) return
                var vmin = tg.minValue, vmax = tg.maxValue
                if (vmax <= vmin) vmax = vmin + 1
                function yOf(v) { return topM + plotH * (1 - (v - vmin) / (vmax - vmin)) }
                var n = Math.max(2, Math.round(pw / 2))
                var vals = tg.samples(tideBar.winStartMs, tideBar.winEndMs, n)
                if (vals.length < 2) return
                if (tg.isCurrent) {   // zero baseline for currents
                    ctx.strokeStyle = "#40ffffff"; ctx.lineWidth = 1
                    ctx.beginPath(); ctx.moveTo(pl, yOf(0)); ctx.lineTo(pl + pw, yOf(0)); ctx.stroke()
                }
                // filled curve
                ctx.beginPath(); ctx.moveTo(pl, yOf(vals[0]))
                for (var i = 1; i < vals.length; i++)
                    ctx.lineTo(pl + pw * i / (vals.length - 1), yOf(vals[i]))
                var baseY = tg.isCurrent ? yOf(0) : (topM + plotH)
                ctx.lineTo(pl + pw, baseY); ctx.lineTo(pl, baseY); ctx.closePath()
                ctx.fillStyle = "#22384f"; ctx.fill()
                ctx.beginPath(); ctx.moveTo(pl, yOf(vals[0]))
                for (var j = 1; j < vals.length; j++)
                    ctx.lineTo(pl + pw * j / (vals.length - 1), yOf(vals[j]))
                ctx.strokeStyle = "#5bb0ff"; ctx.lineWidth = 2; ctx.stroke()
                // current set vectors along the curve, every 15 min: a line in
                // the compass set (N up), length proportional to drift speed.
                if (tg.isCurrent) {
                    var arr = tg.currentArrows(tideBar.winStartMs, tideBar.winEndMs, 15)
                    var span2 = tideBar.winEndMs - tideBar.winStartMs
                    ctx.strokeStyle = "#ffb347"; ctx.fillStyle = "#ffb347"; ctx.lineWidth = 1
                    for (var a = 0; a < arr.length; a++) {
                        // length = drift over the configured time (same setting
                        // as the chart arrows), at a fixed graph scale.
                        var L = Math.min(34, arr[a].spd
                                         * (DisplayConfig.currentVectorMinutes / 60) * 130)
                        if (L < 1.5) continue   // skip near-slack (too short to read)
                        var ax = pl + pw * (arr[a].t - tideBar.winStartMs) / span2
                        var ay = yOf(arr[a].v)
                        var rad = arr[a].dir * Math.PI / 180
                        var ux = Math.sin(rad), uy = -Math.cos(rad)   // compass -> screen (N up)
                        var tx = ax + ux * L, ty = ay + uy * L
                        ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(tx, ty); ctx.stroke()
                        var nx = -uy, ny = ux                          // arrowhead
                        ctx.beginPath(); ctx.moveTo(tx, ty)
                        ctx.lineTo(tx - ux * 4 + nx * 2.2, ty - uy * 4 + ny * 2.2)
                        ctx.lineTo(tx - ux * 4 - nx * 2.2, ty - uy * 4 - ny * 2.2)
                        ctx.closePath(); ctx.fill()
                    }
                }
                // now line -- full height so it continues down into the bar
                var nf = TimeController.nowFraction
                if (nf >= 0 && nf <= 1) {
                    var nx = pl + pw * nf
                    ctx.strokeStyle = "#ff5b5b"; ctx.lineWidth = 1
                    ctx.beginPath(); ctx.moveTo(nx, topM); ctx.lineTo(nx, height); ctx.stroke()
                }
                // fixed read-marker; the value blob sits ON the drawn curve at
                // the marker (interpolate the samples) so it always intersects
                // the curve and the height is read off at the selected time.
                var mx = pl + pw * TimeController.markerFraction
                ctx.strokeStyle = "#ffd27f"; ctx.lineWidth = 2
                ctx.beginPath(); ctx.moveTo(mx, topM); ctx.lineTo(mx, height); ctx.stroke()
                var mIdx = TimeController.markerFraction * (vals.length - 1)
                var i0 = Math.max(0, Math.min(vals.length - 2, Math.floor(mIdx)))
                var mv = vals[i0] + (vals[i0 + 1] - vals[i0]) * (mIdx - i0)
                var my = yOf(mv)
                ctx.fillStyle = "#ffd27f"; ctx.beginPath(); ctx.arc(mx, my, 4.5, 0, 2 * Math.PI); ctx.fill()
                // height/speed read off the curve at the marker -- recomputed
                // every frame from the same samples, so it tracks the blob.
                var mtxt = (tg.isCurrent ? Math.abs(mv) : mv).toFixed(1) + " " + tg.unitLabel
                ctx.fillStyle = "#ffffff"; ctx.font = "bold 12px sans-serif"
                ctx.fillText(mtxt, Math.min(width - 70, mx + 8), Math.max(topM + 12, my - 8))
                // turning-point events (HW/LW or flood/ebb)
                var evs = tg.events(tideBar.winStartMs, tideBar.winEndMs)
                ctx.font = "10px sans-serif"
                for (var k = 0; k < evs.length; k++) {
                    var ex = pl + pw * (evs[k].t - tideBar.winStartMs) / (tideBar.winEndMs - tideBar.winStartMs)
                    var ey = yOf(evs[k].v)
                    ctx.fillStyle = "#bcd8f5"
                    ctx.beginPath(); ctx.arc(ex, ey, 2.5, 0, 2 * Math.PI); ctx.fill()
                    var lbl = evs[k].type + " " + Number(evs[k].v).toFixed(1)
                    var tw = ctx.measureText(lbl).width
                    ctx.fillText(lbl, Math.max(pl, Math.min(pl + pw - tw, ex - tw / 2)), ey - 6)
                }
                // y-axis: a vertical line at t=0 (the left edge of the plot)
                // rising from the x-axis, with right-aligned figures + ticks.
                ctx.strokeStyle = "#6b7a8d"; ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(pl, topM); ctx.lineTo(pl, height); ctx.stroke()
                ctx.fillStyle = "#9fb1c4"; ctx.font = "10px sans-serif"
                ctx.textAlign = "right"; ctx.textBaseline = "middle"
                for (var g = 0; g <= 4; g++) {
                    var vv = vmin + (vmax - vmin) * g / 4
                    var gy = yOf(vv)
                    ctx.beginPath(); ctx.moveTo(pl - 3, gy); ctx.lineTo(pl, gy); ctx.stroke()
                    ctx.fillText(Number(vv).toFixed(1), pl - 6, gy)
                }
                // rotated axis title (replaces the unit that overlapped the top
                // figure): "Height (m)" for tides, "Speed (kn)" for currents.
                ctx.save()
                ctx.translate(9, topM + plotH / 2)
                ctx.rotate(-Math.PI / 2)
                ctx.textAlign = "center"; ctx.textBaseline = "middle"
                ctx.fillStyle = "#c0d0e0"; ctx.font = "10px sans-serif"
                ctx.fillText((tg.isCurrent ? "Speed (" : "Height (") + tg.unitLabel + ")", 0, 0)
                ctx.restore()
                ctx.textAlign = "left"; ctx.textBaseline = "alphabetic"
            }
            Connections {
                target: chart.tideGraph
                function onChanged() { graphCanvas.requestPaint() }
                function onMarkerChanged() { graphCanvas.requestPaint() }
            }
            Connections {
                target: DisplayConfig
                function onChanged() { graphCanvas.requestPaint() }
            }
            Timer {  // smooth repaint while the drawer is open (engine-sampled)
                interval: 33; repeat: true; running: tideDrawer.open
                onTriggered: graphCanvas.requestPaint()
            }
        }

        // Drag anywhere on the graph to pan time as well (mirrors the bar).
        MouseArea {
            anchors.fill: parent
            property real lastX: 0
            property real pressX: 0
            onPressed: (m) => { lastX = m.x; pressX = m.x }
            onReleased: (m) => {
                if (Math.abs(m.x - pressX) < 4 && tideBar.plotWidth > 1)
                    TimeController.setDisplayFraction((m.x - tideBar.plotLeft) / tideBar.plotWidth)
            }
            onPositionChanged: (m) => {
                if (pressed && tideBar.plotWidth > 1) {
                    TimeController.panPixels(m.x - lastX, tideBar.plotWidth)
                    lastX = m.x
                }
            }
        }

        // Header band (on top of the canvas): station name + close.
        Label {
            anchors.top: parent.top; anchors.left: parent.left
            anchors.topMargin: 4; anchors.leftMargin: 8
            width: parent.width - 36
            text: chart.tideGraph && chart.tideGraph.valid
                  ? chart.tideGraph.stationName : ""
            color: "#e8eef6"; font.bold: true; font.pointSize: 10
            elide: Text.ElideRight
        }
        Label {  // plain close glyph (no button background), top-right
            anchors.top: parent.top; anchors.right: parent.right
            anchors.topMargin: 3; anchors.rightMargin: 8
            text: "✕"; font.pointSize: 12
            color: closeMA.containsMouse ? "#ffffff" : "#8a97a6"
            MouseArea {
                id: closeMA; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: if (chart.tideGraph) chart.tideGraph.clear()
            }
        }
    }

    // --- Time bar (P3.14 F): the graph's x-axis. A thin solid strip pinned to
    //     the window bottom (above the status bar). The chart shrinks above it,
    //     and further when the drawer opens. A fixed read-marker over a
    //     pannable, infinite axis (drag to pan; click to read; ▶ animates;
    //     Now re-snaps). Visible only when tides are on (MUIBar ≋).
    Item {
        id: tideBar
        anchors.left: parent.left
        anchors.right: hudPanel.left
        anchors.bottom: parent.bottom
        height: DisplayConfig.showTides ? 30 : 0
        visible: DisplayConfig.showTides
        clip: true
        Behavior on height { NumberAnimation { duration: 150 } }

        // Single source of truth for the time->x mapping, consumed by the bar
        // ticks AND the graph Canvas. The plot starts past a left gutter so the
        // graph's y-axis lines up with the window's left edge.
        readonly property real plotLeft: plot.x
        readonly property real plotWidth: plot.width
        readonly property double winStartMs: TimeController.windowStart.getTime()
        readonly property double winEndMs: TimeController.windowEnd.getTime()

        Rectangle {  // one continuous panel with the drawer -- no borders/seams
            anchors.fill: parent
            color: "#0b1118"
        }

        Label {  // play/pause in the bottom-left corner (where the axes meet)
            id: playBtn
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 8
            text: TimeController.playing ? "⏸" : "▶"
            color: playMA.containsMouse ? "#ffffff" : "#d8e2ec"; font.pointSize: 13
            MouseArea {
                id: playMA; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: TimeController.togglePlay()
            }
        }

        Item {  // plot: x-axis ticks + fixed marker + now line + readout + pan
            id: plot
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: parent.top; anchors.bottom: parent.bottom
            anchors.leftMargin: 44   // y-axis gutter (axis title + figures)
            anchors.rightMargin: 8

            Canvas {
                id: barCanvas
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d"); ctx.reset()
                    var s = tideBar.winStartMs, e = tideBar.winEndMs
                    if (e <= s) return
                    var span = e - s
                    function xOf(ms) { return width * (ms - s) / span }
                    var d = new Date(s)
                    d.setMinutes(0, 0, 0); d.setHours(d.getHours() + 1)
                    for (var t = d.getTime(); t < e; t += 3600000) {
                        var x = xOf(t)
                        var hr = new Date(t).getHours()
                        var major = (hr % 3 === 0)
                        // ticks hang from the top edge (the graph baseline)
                        ctx.strokeStyle = major ? "#80ffffff" : "#38ffffff"; ctx.lineWidth = 1
                        ctx.beginPath()
                        ctx.moveTo(x, 0); ctx.lineTo(x, major ? 9 : 5); ctx.stroke()
                        if (major) {
                            ctx.fillStyle = "#b0c0d0"; ctx.font = "9px sans-serif"
                            ctx.fillText((hr < 10 ? "0" : "") + hr, x - 6, height - 3)
                        }
                    }
                }
                Connections {
                    target: TimeController
                    function onWindowChanged() { barCanvas.requestPaint() }
                }
            }

            Rectangle {  // now line
                visible: TimeController.nowFraction >= 0 && TimeController.nowFraction <= 1
                x: plot.width * TimeController.nowFraction - 1
                width: 2; height: plot.height; color: "#ff5b5b"
            }
            Rectangle {  // fixed read-marker (the axis pans under it)
                x: plot.width * TimeController.markerFraction - 1
                width: 2; height: plot.height; color: "#ffd27f"
            }

            MouseArea {
                id: panArea
                anchors.fill: parent
                property real lastX: 0
                property real pressX: 0
                onPressed: (m) => { lastX = m.x; pressX = m.x }
                onReleased: (m) => {
                    if (Math.abs(m.x - pressX) < 4)
                        TimeController.setDisplayFraction(m.x / plot.width)
                }
                onPositionChanged: (m) => {
                    if (pressed) {
                        TimeController.panPixels(m.x - lastX, plot.width)
                        lastX = m.x
                    }
                }
            }

            // Date/time readout pinned beside the gold marker; double-click =
            // snap to Now (frees the right side for a full-width plot).
            Rectangle {
                id: readout
                x: Math.min(plot.width - width - 2,
                            plot.width * TimeController.markerFraction + 6)
                y: 1; height: 15; width: readoutText.width + 8; radius: 2
                color: Qt.rgba(0, 0, 0, 0.55)
                Text {
                    id: readoutText; anchors.centerIn: parent
                    text: TimeController.dateLabel + " " + TimeController.timeLabel
                    color: TimeController.live ? "#8fd0ff" : "#ffd27f"
                    font.pointSize: 9; font.bold: true
                }
                MouseArea {
                    anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    property real lastX: 0
                    onPressed: (m) => lastX = m.x
                    onDoubleClicked: TimeController.goLive()
                    onPositionChanged: (m) => {
                        if (pressed) {
                            TimeController.panPixels(m.x - lastX, plot.width); lastX = m.x
                        }
                    }
                    ToolTip.text: qsTr("Double-click to snap to now")
                    ToolTip.visible: containsMouse; ToolTip.delay: 600
                }
            }
        }
    }

    // --- Vessel data HUD: expandable right-edge panel. A SIBLING of the
    //     chart (the chart anchors its right edge to this), so opening it
    //     shrinks the chart rather than covering it. Gauges stacked
    //     vertically; future "pages" can swap the stack.
    Item {
        id: hudPanel
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: app.hudExpanded ? 300 : 0
        clip: true
        Behavior on width { NumberAnimation { duration: 150 } }

        readonly property var nav: chart.navState
        // Heading if available, else COG, for the boat/COG indicator.
        readonly property real hdg: nav && nav.hdgValid ? nav.hdg
                                   : (nav ? nav.cog : 0)

        Rectangle {
            visible: app.hudExpanded
            anchors.fill: parent
            color: "#ee0e1216"; border.color: "#3affffff"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Label {
                    text: hudPanel.nav ? hudPanel.nav.positionText : "---"
                    color: "#b0d0ff"; font.pointSize: 12; font.family: "monospace"
                    Layout.alignment: Qt.AlignHCenter
                }

                // --- Gauge 1: compass with SOG/COG (north-up). ---
                Canvas {
                    id: cogGauge
                    Layout.alignment: Qt.AlignHCenter
                    width: 250; height: 250
                    property real cog: hudPanel.nav ? hudPanel.nav.cog : 0
                    property real sog: hudPanel.nav ? hudPanel.nav.sog : 0
                    property bool valid: hudPanel.nav && hudPanel.nav.ownShipValid
                    property real hdg: hudPanel.hdg
                    property bool hdgValid: hudPanel.nav && hudPanel.nav.hdgValid
                    onCogChanged: requestPaint()
                    onSogChanged: requestPaint()
                    onValidChanged: requestPaint()
                    onHdgChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d"); ctx.reset()
                        var cx = width/2, cy = height/2, r = width/2 - 6
                        var d2r = Math.PI/180
                        ctx.strokeStyle = "#c8d0d8"; ctx.lineWidth = 1.5
                        ctx.beginPath(); ctx.arc(cx, cy, r, 0, 2*Math.PI); ctx.stroke()
                        // Ticks + labels every 30 deg (north-up).
                        ctx.textAlign = "center"; ctx.textBaseline = "middle"
                        for (var d = 0; d < 360; d += 10) {
                            var a = (d - 90) * d2r
                            var major = (d % 30 === 0)
                            var inner = r - (major ? 14 : 7)
                            ctx.strokeStyle = (d > 0 && d < 180) ? "#4faf3a" : "#c83a3a"
                            ctx.lineWidth = major ? 2.5 : 1
                            ctx.beginPath()
                            ctx.moveTo(cx + inner*Math.cos(a), cy + inner*Math.sin(a))
                            ctx.lineTo(cx + r*Math.cos(a), cy + r*Math.sin(a))
                            ctx.stroke()
                            if (major) {
                                var lbl = (d===0?"N":d===90?"E":d===180?"S":d===270?"W":""+d)
                                var card = (d%90===0)
                                ctx.fillStyle = card ? "#c83a3a" : "#404850"
                                ctx.font = (card?"bold ":"") + "13px sans-serif"
                                var lr = r - 28
                                ctx.fillText(lbl, cx + lr*Math.cos(a), cy + lr*Math.sin(a))
                            }
                        }
                        // COG needle (orange wedge from centre to rim).
                        if (cogGauge.valid) {
                            var ca = (cogGauge.cog - 90) * d2r
                            ctx.fillStyle = "#e8651e"
                            ctx.beginPath()
                            ctx.moveTo(cx + r*Math.cos(ca), cy + r*Math.sin(ca))
                            ctx.lineTo(cx + 14*Math.cos(ca+1.55), cy + 14*Math.sin(ca+1.55))
                            ctx.lineTo(cx + 14*Math.cos(ca-1.55), cy + 14*Math.sin(ca-1.55))
                            ctx.closePath(); ctx.fill()
                        }
                        // HDG needle (white) drawn over the COG wedge.
                        if (cogGauge.hdgValid) {
                            var ha = (cogGauge.hdg - 90) * d2r
                            ctx.strokeStyle = "#ffffff"; ctx.lineWidth = 3
                            ctx.beginPath(); ctx.moveTo(cx, cy)
                            ctx.lineTo(cx + r*Math.cos(ha), cy + r*Math.sin(ha))
                            ctx.stroke()
                        }
                        // Centre hub with SOG + COG readout.
                        ctx.fillStyle = "#3a4048"
                        ctx.beginPath(); ctx.arc(cx, cy, r*0.55, 0, 2*Math.PI); ctx.fill()
                        ctx.fillStyle = "#aab4be"; ctx.font = "12px sans-serif"
                        ctx.fillText("SOG", cx, cy - r*0.30)
                        ctx.fillStyle = "#8fd14f"; ctx.font = "bold 26px sans-serif"
                        ctx.fillText(cogGauge.valid ? cogGauge.sog.toFixed(1)+" kt" : "-- kt",
                                     cx, cy - r*0.07)
                        ctx.fillStyle = "#aab4be"; ctx.font = "12px sans-serif"
                        ctx.fillText("COG", cx, cy + r*0.18)
                        ctx.fillStyle = "#ffffff"; ctx.font = "bold 22px sans-serif"
                        ctx.fillText(cogGauge.valid
                            ? (("00"+Math.round(cogGauge.cog)).slice(-3))+"°T" : "---°T",
                            cx, cy + r*0.36)
                    }
                }
                // STW | HDG numerics under the compass.
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: hudPanel.nav && hudPanel.nav.stw >= 0
                                  ? hudPanel.nav.stw.toFixed(2) : "--"
                            color:"#e8e8e8"; font.pointSize: 22; Layout.alignment: Qt.AlignHCenter
                        }
                        Label { text: "STW  kt"; color:"#808890"; font.pointSize: 10; Layout.alignment: Qt.AlignHCenter }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: hudPanel.nav && hudPanel.nav.hdgValid
                                  ? hudPanel.nav.hdg.toFixed(0) : "--"
                            color:"#e8e8e8"; font.pointSize: 22; Layout.alignment: Qt.AlignHCenter
                        }
                        Label { text: "HDG  °T"; color:"#808890"; font.pointSize: 10; Layout.alignment: Qt.AlignHCenter }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#30ffffff" }

                // --- Gauge 2: apparent wind rose (bow-up). No wind feed
                //     yet (#39) -- drawn with the needle hidden + "--". ---
                Canvas {
                    id: windGauge
                    Layout.alignment: Qt.AlignHCenter
                    width: 250; height: 250
                    property real awa: hudPanel.nav && hudPanel.nav.awaValid ? hudPanel.nav.awa : -1
                    property real aws: hudPanel.nav ? hudPanel.nav.aws : -1
                    property real twa: hudPanel.nav && hudPanel.nav.twaValid ? hudPanel.nav.twa : -1
                    property real tws: hudPanel.nav ? hudPanel.nav.tws : -1
                    onAwaChanged: requestPaint()
                    onAwsChanged: requestPaint()
                    onTwaChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d"); ctx.reset()
                        var cx = width/2, cy = height/2, r = width/2 - 6
                        var d2r = Math.PI/180
                        ctx.strokeStyle = "#c8d0d8"; ctx.lineWidth = 1.5
                        ctx.beginPath(); ctx.arc(cx, cy, r, 0, 2*Math.PI); ctx.stroke()
                        ctx.textAlign = "center"; ctx.textBaseline = "middle"
                        // Bow-up: 0 at top, 180 at bottom; both sides.
                        for (var d = 0; d <= 360; d += 10) {
                            var a = (d - 90) * d2r
                            var major = (d % 30 === 0)
                            var inner = r - (major ? 14 : 7)
                            // Port (left, 180..360) red; stbd (right, 0..180) green.
                            ctx.strokeStyle = (d > 0 && d < 180) ? "#4faf3a" : "#c83a3a"
                            ctx.lineWidth = major ? 2.5 : 1
                            ctx.beginPath()
                            ctx.moveTo(cx + inner*Math.cos(a), cy + inner*Math.sin(a))
                            ctx.lineTo(cx + r*Math.cos(a), cy + r*Math.sin(a))
                            ctx.stroke()
                            if (major) {
                                var val = d <= 180 ? d : 360 - d  // mirror to 0..180
                                ctx.fillStyle = "#404850"; ctx.font = "13px sans-serif"
                                var lr = r - 26
                                ctx.fillText(""+val, cx + lr*Math.cos(a), cy + lr*Math.sin(a))
                            }
                        }
                        // Boat outline at centre (bow up).
                        ctx.strokeStyle = "#404850"; ctx.lineWidth = 2
                        ctx.beginPath()
                        ctx.moveTo(cx, cy - r*0.42)
                        ctx.quadraticCurveTo(cx + r*0.20, cy - r*0.10, cx + r*0.16, cy + r*0.30)
                        ctx.lineTo(cx - r*0.16, cy + r*0.30)
                        ctx.quadraticCurveTo(cx - r*0.20, cy - r*0.10, cx, cy - r*0.42)
                        ctx.stroke()
                        // TWA needle (blue) drawn first, then AWA on top.
                        if (windGauge.twa >= 0) {
                            var ta = (windGauge.twa - 90) * d2r
                            ctx.strokeStyle = "#3a8fd1"; ctx.lineWidth = 4
                            ctx.beginPath(); ctx.moveTo(cx, cy)
                            ctx.lineTo(cx + r*0.80*Math.cos(ta), cy + r*0.80*Math.sin(ta))
                            ctx.stroke()
                        }
                        // AWA needle (green) on top.
                        if (windGauge.awa >= 0) {
                            var wa = (windGauge.awa - 90) * d2r
                            ctx.strokeStyle = "#8fd14f"; ctx.lineWidth = 4
                            ctx.beginPath(); ctx.moveTo(cx, cy)
                            ctx.lineTo(cx + r*0.80*Math.cos(wa), cy + r*0.80*Math.sin(wa))
                            ctx.stroke()
                        }
                        ctx.fillStyle = "#8fd14f"; ctx.font = "bold 22px sans-serif"
                        ctx.fillText(windGauge.aws >= 0 ? windGauge.aws.toFixed(1)+" kt" : "-- kt",
                                     cx, cy + r*0.55)
                        ctx.fillStyle = "#9a9a3a"; ctx.font = "13px sans-serif"
                        ctx.fillText("AWA", cx, cy + r*0.74)
                    }
                }
                // TWS | TWA numerics under the wind rose.
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: hudPanel.nav && hudPanel.nav.tws >= 0
                                  ? hudPanel.nav.tws.toFixed(1) : "--"
                            color:"#e8e8e8"; font.pointSize: 22; Layout.alignment: Qt.AlignHCenter
                        }
                        Label { text: "TWS  kt"; color:"#808890"; font.pointSize: 10; Layout.alignment: Qt.AlignHCenter }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: hudPanel.nav && hudPanel.nav.twaValid
                                  ? hudPanel.nav.twa.toFixed(0) : "--"
                            color:"#e8e8e8"; font.pointSize: 22; Layout.alignment: Qt.AlignHCenter
                        }
                        Label { text: "TWA  °"; color:"#808890"; font.pointSize: 10; Layout.alignment: Qt.AlignHCenter }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    // --- Floating master toolbar (FloatToolbar.qml, P3.17). The persistent
    //     vertical control column; tools that open a window/drawer are wired
    //     via signals. Layout per the agreed toolbar spec: Tides + Anchor
    //     relocated here from the MUI bar, MOB icon fixed to the life-buoy.
    FloatToolbar {
        x: 16
        y: 16
        touchSize: root.touchSize
        onOptionsRequested: { optionsWindow.show(); optionsWindow.raise() }
        onRouteManagerRequested: routeDrawer.opened ? routeDrawer.close()
                                                     : routeDrawer.open()
        onDataMonitorRequested: { dataMonitorWindow.show(); dataMonitorWindow.raise() }
        onAboutRequested: { aboutWindow.show(); aboutWindow.raise() }
        onAnchorWatchRequested: anchorPopup.open()
    }

    // Colour-scheme dim overlay (#30): tints the whole window for dusk/night,
    // mirroring how OpenCPN dims the DisplayConfig. Plain item, input-transparent
    // (enabled:false) so it never intercepts chart/toolbar interaction.
    Rectangle {
        anchors.fill: parent
        enabled: false
        visible: chart.colorScheme !== 0
        color: chart.colorScheme === 2 ? "#66200000"   // night: dark red wash
                                       : "#400a1432"   // dusk: dark blue wash
    }

}
