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
        onDataMonitorRequested: {
            dataMonitorWindow.show(); dataMonitorWindow.raise()
        }
    }

    // --- Send to Peer (SendToPeerDialog.qml, P3.18 tier 4): mDNS-discovered
    //     OpenCPN instances + route/mark/track transfer with PIN pairing.
    SendToPeerDialog { id: sendToPeerDialog }

    // --- Send to GPS (SendToGpsDialog.qml, P3.18 tier 4): serial NMEA-0183
    //     route/mark upload to a plotter or handheld.
    SendToGpsDialog { id: sendToGpsDialog }

    // --- AIS target list (AisTargetListWindow.qml, P3.18): live targets
    //     nearest-first; opened from the AIS right-click menu.
    AisTargetListWindow { id: aisTargetListWindow }

    // --- About (AboutWindow.qml, P3.17): mirrors the wx About dialog. ---
    AboutWindow { id: aboutWindow }

    // --- Data Monitor (DataMonitorWindow.qml, P3.17): live decoded NMEA/N2K
    //     stream with pause + per-source filter.
    DataMonitorWindow { id: dataMonitorWindow }

    // --- Central: world-anchored + display-anchored scene-graph subtrees,
    //     both inside the ChartCanvas QQuickItem.
    // P6.1 focused-canvas: which pane toolbar/zoom actions act on. Follows
    // keyboard focus (the canvases focus on click); defaults to the
    // primary chart.
    property var activeChart: chart

    // P6.1 (experimental): an independent second chart pane sharing the
    // chart library but with its own viewport, decode engine and worker.
    // Inspection-only in v1 (no route editing UI on this pane).
    ChartCanvas {
        id: splitPane
        visible: UIConfig.splitView && s52SplitPane !== null
        onActiveFocusChanged: if (activeFocus) root.activeChart = splitPane
        // App-wide colour scheme: the second pane mirrors the primary.
        colorScheme: chart.colorScheme
        width: visible ? parent.width * UIConfig.splitFraction : 0
        anchors.top: parent.top
        anchors.bottom: tideDrawer.top
        anchors.right: hudPanel.left
        clip: true
        s52Engine: s52SplitPane
        // P6.1 per-pane editing: this pane's own context menus, acting on
        // splitPane and sharing the app dialogs.
        ChartContextMenus {
            targetChart: splitPane
            onObjectQueryRequested: {
                objectQueryWindow.show(); objectQueryWindow.raise()
            }
            onNewMarkRequested: {
                markEditor.targetChart = splitPane
                markEditor.openNew()
            }
            onAisTargetListRequested: {
                aisListWindow.show(); aisListWindow.raise()
            }
            onFullScreenRequested: root.visibility === Window.FullScreen
                                   ? root.showNormal() : root.showFullScreen()
        }

        Rectangle {  // divider (drag to resize; fraction persists)
            width: 2
            color: dividerDrag.active ? "#cc3b82f6" : "#55202830"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            MouseArea {
                id: dividerDrag
                property bool active: pressed
                anchors.fill: parent
                anchors.margins: -4  // fatter hit target
                cursorShape: Qt.SplitHCursor
                onPositionChanged: (m) => {
                    if (!pressed) return
                    const rootX = mapToItem(splitPane.parent, m.x, 0).x
                    UIConfig.splitFraction =
                        1.0 - rootX / splitPane.parent.width
                }
            }
        }
    }

    ChartCanvas {
        id: chart
        focus: true  // canvas keyboard layer (P3.20): arrows pan, +/- zoom, …
        onActiveFocusChanged: if (activeFocus) root.activeChart = chart
        clip: true   // never rasterise chart geometry into the tide drawer below
        anchors.left: parent.left
        anchors.top: parent.top
        // Bottom follows the tide graph drawer (which sits on the time bar);
        // the chart shrinks for the bar, then further as the drawer opens.
        anchors.bottom: tideDrawer.top
        // Right edge follows the HUD panel so opening it shrinks the chart
        // -- or the experimental second pane (P6.1) when split view is on.
        anchors.right: splitPane.visible ? splitPane.left : hudPanel.left
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
            // Draggable (with the nav pill riding underneath); position
            // persists as canvas fractions, default top-right.
            x: UIConfig.hudStatsX >= 0
               ? UIConfig.hudStatsX * (parent.width - width)
               : parent.width - width - 12
            y: UIConfig.hudStatsY >= 0
               ? UIConfig.hudStatsY * (parent.height - height) : 12
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

            // Click the rose to cycle North-Up -> Course-Up -> Head-Up;
            // drag it to move the whole HUD group (pill rides along).
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                drag.target: compass
                drag.threshold: 6
                drag.minimumX: 0
                drag.maximumX: compass.parent.width - compass.width
                drag.minimumY: 0
                drag.maximumY: compass.parent.height - compass.height
                property bool dragged: false
                onPositionChanged: if (drag.active) dragged = true
                onReleased: {
                    // drag.active can already be false here -- track moves.
                    if (!dragged) return
                    dragged = false
                    UIConfig.hudStatsX = compass.x /
                        Math.max(1, compass.parent.width - compass.width)
                    UIConfig.hudStatsY = compass.y /
                        Math.max(1, compass.parent.height - compass.height)
                }
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
            anchors.top: compass.visible ? compass.bottom : parent.top
            anchors.right: compass.visible ? compass.right : parent.right
            anchors.topMargin: compass.visible ? 8 : 12
            anchors.rightMargin: compass.visible ? -((width - compass.width) / 2) : 12
            width: hudCol.implicitWidth + 24
            height: hudCol.implicitHeight + 16
            radius: 6
            color: "#cc101418"
            border.color: "#3affffff"

            readonly property var nav: chart.navState
            // Active-route following solution (P3.16); null-safe via `active`.
            readonly property var rf: chart.routeFollower

            // The pick-able stat rows (right-click / long-press to choose).
            // Values render in the pill's existing row style.
            readonly property var statCatalog: [
                { key: "sog", label: qsTr("SOG") },
                { key: "cog", label: qsTr("COG") },
                { key: "hdg", label: qsTr("HDG") },
                { key: "stw", label: qsTr("STW") },
                { key: "awa", label: qsTr("AWA") },
                { key: "aws", label: qsTr("AWS") },
                { key: "twa", label: qsTr("TWA") },
                { key: "tws", label: qsTr("TWS") },
                { key: "dpt", label: qsTr("DPT") },
                { key: "mtw", label: qsTr("SEA") },
                { key: "pos", label: qsTr("POS") },
            ]
            function statValue(key) {
                const n = navHud.nav
                switch (key) {
                case "sog": return n ? n.sogText : "--"
                case "cog": return n ? n.cogText : "--"
                case "hdg": return n && n.hdgValid
                    ? ("00" + Math.round(n.hdg)).slice(-3) + "°" : "--"
                case "stw": return n && n.stw >= 0
                    ? n.stw.toFixed(1) + " kn" : "--"
                case "awa": return n && n.awaValid
                    ? Math.round(n.awa) + "°" : "--"
                case "aws": return n && n.aws >= 0
                    ? n.aws.toFixed(1) + " kn" : "--"
                case "twa": return n && n.twaValid
                    ? Math.round(n.twa) + "°" : "--"
                case "tws": return n && n.tws >= 0
                    ? n.tws.toFixed(1) + " kn" : "--"
                case "dpt": return chart.depthText
                case "mtw": return chart.waterTempText
                default: return ""
                }
            }

            // Right-click opens the stat picker (checkable menu).
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: statPicker.popup()
            }
            Menu {
                id: statPicker
                title: qsTr("HUD values")
                Instantiator {
                    model: navHud.statCatalog
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.label
                        checkable: true
                        checked: UIConfig.hudStats.indexOf(modelData.key) >= 0
                        onTriggered: {
                            var l = UIConfig.hudStats.slice()
                            if (l.indexOf(modelData.key) >= 0)
                                l.splice(l.indexOf(modelData.key), 1)
                            else l.push(modelData.key)
                            UIConfig.hudStats = navHud.statCatalog
                                .map((s) => s.key)
                                .filter((k) => l.indexOf(k) >= 0)
                        }
                        onCheckedChanged: { }  // driven by the binding
                    }
                    onObjectAdded: (i, o) => statPicker.insertItem(i, o)
                    onObjectRemoved: (i, o) => statPicker.removeItem(o)
                }
            }

            Column {
                id: hudCol
                anchors.centerIn: parent
                spacing: 2

                Repeater {
                    model: navHud.statCatalog.filter(
                               (st) => st.key !== "pos" &&
                                       UIConfig.hudStats.indexOf(st.key) >= 0)
                    delegate: Text {
                        required property var modelData
                        text: modelData.label + "  " +
                              navHud.statValue(modelData.key)
                        color: "#e0e0e0"; font.pointSize: 13; font.bold: true
                    }
                }
                Text {
                    visible: UIConfig.hudStats.indexOf("pos") >= 0
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

        // --- Depth-units legend (P2.20, wx "Show depth units"): the sounding
        //     unit every figure on screen is in, beside the scale bar.
        Rectangle {
            visible: DisplayConfig.showDepthUnits && scaleBar.visible
            anchors.left: scaleBar.right
            anchors.bottom: scaleBar.bottom
            anchors.leftMargin: 6
            width: depthUnitText.implicitWidth + 14
            height: depthUnitText.implicitHeight + 10
            radius: 4
            color: "#cc101418"
            border.color: "#3affffff"
            Text {
                id: depthUnitText
                anchors.centerIn: parent
                text: qsTr("Depths: ") + [qsTr("m"), qsTr("ft"),
                                          qsTr("fm")][DisplayConfig.depthUnit]
                color: "#e8f0ff"; font.pointSize: 10
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
                        onClicked: {
                            chart.alerts.dropAnchor()
                            // wx parity: optionally pin an anchor mark too.
                            if (RouteDefaultsConfig.autoAnchorMark &&
                                    chart.alerts.anchorSet)
                                chart.dropAnchorMark(chart.alerts.anchorLat,
                                                     chart.alerts.anchorLon)
                        }
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

        // Canvas right-click menus (ChartContextMenus.qml, P3.18): the general
        // chart menu + focused route / mark / route-node menus, popped by the
        // ChartCanvas hit-test signals. Sibling dialogs are wired here.
        ChartContextMenus {
            onObjectQueryRequested: {
                objectQueryWindow.show(); objectQueryWindow.raise()
            }
            onNewMarkRequested: {
                markEditor.targetChart = chart
                markEditor.openNew()
            }
            onEditMarkRequested: (guid) => {
                const wps = chart.routeList.waypoints
                for (let i = 0; i < wps.length; ++i) {
                    if (wps[i].guid === guid) {
                        markEditor.openForEdit(wps[i].guid, wps[i].name,
                                               wps[i].comment, wps[i].icon)
                        return
                    }
                }
            }
            onRouteDetailsRequested: (idx) => {
                const rts = chart.routeList.routes
                if (idx >= 0 && idx < rts.length)
                    routeDetailsDialog.openFor(idx, rts[idx].name)
            }
            onAisTargetListRequested: {
                aisTargetListWindow.show(); aisTargetListWindow.raise()
            }
            onFullScreenRequested: root.visibility === Window.FullScreen
                                   ? root.showNormal() : root.showFullScreen()
            onSendRouteToPeerRequested: (idx) => {
                const rts = chart.routeList.routes
                sendToPeerDialog.openForRoute(
                    idx, idx >= 0 && idx < rts.length ? rts[idx].name : "")
            }
            onSendMarkToPeerRequested: (guid, name) =>
                sendToPeerDialog.openForMark(guid, name)
            onSendTrackToPeerRequested: (guid, name) =>
                sendToPeerDialog.openForTrack(guid, name)
            onSendRouteToGpsRequested: (idx) => {
                const rts = chart.routeList.routes
                sendToGpsDialog.openForRoute(
                    idx, idx >= 0 && idx < rts.length ? rts[idx].name : "")
            }
            onSendMarkToGpsRequested: (guid, name) =>
                sendToGpsDialog.openForMark(guid, name)
        }

        // Plugin HUD contributions (P4.2): each registered component loads
        // above the chart with its plugin context attached.
        Repeater {
            model: chart.pluginRegistry.hudComponents
            delegate: Loader {
                required property var modelData
                source: modelData.component
                onLoaded: if (item && modelData.context)
                              item.pluginContext = modelData.context
            }
        }

        // Measure-tool readout (P3.18): the running leg bearing/distance +
        // total, pinned top-centre while measuring. Esc or the context menu
        // ("Measure off") ends the measurement.
        Rectangle {
            visible: chart.measureActive
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 10
            width: measureRow.implicitWidth + 24
            height: measureRow.implicitHeight + 12
            radius: height / 2
            color: "#cc0b1118"
            border.color: "#ffc83c"
            Row {
                id: measureRow
                anchors.centerIn: parent
                spacing: 10
                Label {
                    text: "📐 " + chart.measureText
                    color: "#ffe9a8"
                    font.pointSize: 12
                }
                Label {
                    text: qsTr("Esc to end")
                    color: "#8a8f98"
                    font.pointSize: 10
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
        Shortcut {
            sequence: "Escape"
            enabled: chart.measureActive
            onActivated: chart.stopMeasure()
        }

        // (The old selection-bound "Editing route…" hint was removed: selection
        // and edit are now distinct -- the edit banner above is gated on
        // chart.routeEditMode, P3.7.)
    }

    // --- Tide/current graph (TideGraphPanel.qml, P3.14 F/P3.17): grows UP
    //     out of the time bar so the bar's hour ticks are the graph's x-axis.
    TideGraphPanel {
        id: tideDrawer
        timeBar: tideBar
        anchors.left: parent.left
        anchors.right: hudPanel.left
        anchors.bottom: tideBar.top
    }

    // --- Time bar (TideTimeBar.qml, P3.14 F/P3.17): the tide graph's x-axis,
    //     pinned to the window bottom; owns the time->x mapping.
    TideTimeBar {
        id: tideBar
        anchors.left: parent.left
        anchors.right: hudPanel.left
        anchors.bottom: parent.bottom
    }

    // --- Vessel data HUD (VesselHud.qml, P3.17): expandable right-edge
    //     panel; a SIBLING of the chart, which anchors its right edge here.
    VesselHud { id: hudPanel }

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
        onAboutRequested: { aboutWindow.show(); aboutWindow.raise() }
        onAnchorWatchRequested: anchorPopup.open()
    }

    // Full-screen toggle (P3.20, wx F11). A window-level Shortcut: function
    // keys can't collide with text entry, unlike the canvas letter keys.
    Shortcut {
        sequences: ["F11"]
        onActivated: root.visibility === Window.FullScreen
                     ? root.showNormal() : root.showFullScreen()
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
