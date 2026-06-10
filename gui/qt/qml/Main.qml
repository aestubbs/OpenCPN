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
                    onTapped: {
                        scaleEntry.text = chart.scaleText.replace(/^.*:/, "")
                        scaleDialog.open()
                        scaleEntry.forceActiveFocus()
                        scaleEntry.selectAll()
                    }
                }
            }
        }
    }

    // Scale entry: clicking the status-bar scale opens this to type a 1:N
    // value. Free-form like wx (mui_bar.cpp OnScaleSelected); ChartCanvas
    // clamps to 1:1,000 .. 1:3,000,000 (no snapping to standard scales).
    Dialog {
        id: scaleDialog
        title: qsTr("Set chart scale")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            const n = parseInt(scaleEntry.text, 10)
            if (!isNaN(n) && n > 0) chart.setScaleDenominator(n)
        }
        RowLayout {
            Label { text: "1:" }
            TextField {
                id: scaleEntry
                Layout.preferredWidth: 140
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1000; top: 3000000 }
                selectByMouse: true
                onAccepted: scaleDialog.accept()
            }
        }
    }

    // --- Canvas options: slide-out display panel from the right (mirrors
    //     OpenCPN's MUIBar CanvasOptions). Native right-edge Drawer.
    // Shared "Vector chart detail" checklist -- used by BOTH the quick
    // pull-out drawer (MUIBar canvas options) and the Options > Charts >
    // Vector Display tab, so the two surfaces can never drift (the bug that
    // left the drawer showing the old 4-toggle list). Soundings is a live
    // ChartCanvas toggle; the rest bind ChartConfig (re-decode via
    // applyChartConfig). Mirrors the wx "Vector Chart Display" detail list.
    component VectorDetailList: ColumnLayout {
        spacing: 6
        Label { text: qsTr("Vector chart detail"); font.bold: true }
        CheckBox {
            text: qsTr("Soundings")
            checked: chart.showSoundings
            onToggled: chart.showSoundings = checked
        }
        CheckBox {
            text: qsTr("Chart information objects")
            checked: ChartConfig.chartInfoObjects
            onToggled: ChartConfig.chartInfoObjects = checked
        }
        CheckBox {
            text: qsTr("Show chart data quality")
            checked: ChartConfig.dataQuality
            onToggled: ChartConfig.dataQuality = checked
        }
        CheckBox {
            text: qsTr("Buoy / light labels")
            checked: ChartConfig.buoyLightLabels
            onToggled: ChartConfig.buoyLightLabels = checked
        }
        CheckBox {
            text: qsTr("Light descriptions")
            checked: ChartConfig.lightDescriptions
            onToggled: ChartConfig.lightDescriptions = checked
        }
        CheckBox {
            text: qsTr("Extended light sectors")
            checked: ChartConfig.extendedLightSectors
            onToggled: ChartConfig.extendedLightSectors = checked
        }
        CheckBox {
            text: qsTr("National text")
            checked: ChartConfig.nationalText
            onToggled: ChartConfig.nationalText = checked
        }
        CheckBox {
            text: qsTr("Important text only")
            checked: ChartConfig.importantTextOnly
            onToggled: ChartConfig.importantTextOnly = checked
        }
        CheckBox {
            text: qsTr("De-cluttered text")
            checked: ChartConfig.declutterText
            onToggled: ChartConfig.declutterText = checked
        }
        CheckBox {
            text: qsTr("Reduced detail at small scale")
            checked: ChartConfig.reducedDetailSmallScale
            onToggled: ChartConfig.reducedDetailSmallScale = checked
        }
        CheckBox {
            text: qsTr("Additional detail reduction (super SCAMIN)")
            checked: ChartConfig.superScamin
            onToggled: ChartConfig.superScamin = checked
        }
    }

    Drawer {
        id: canvasOptions
        edge: Qt.RightEdge
        width: Math.min(320, root.width * 0.85)
        height: root.height

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 8

            Label { text: qsTr("Chart display category"); font.bold: true }
            ButtonGroup { id: catGroup }
            Repeater {
                model: [ { label: qsTr("Base"), cat: 0 },
                         { label: qsTr("Standard"), cat: 1 },
                         { label: qsTr("All"), cat: 2 } ]
                delegate: RadioButton {
                    required property var modelData
                    text: modelData.label
                    ButtonGroup.group: catGroup
                    checked: chart.displayCategory === modelData.cat
                    onClicked: chart.displayCategory = modelData.cat
                }
            }

            MenuSeparator { Layout.fillWidth: true }

            // Same vector-detail checklist as Options > Charts > Vector
            // Display, via the shared VectorDetailList component (kept in sync).
            VectorDetailList { Layout.fillWidth: true }

            MenuSeparator { Layout.fillWidth: true }

            // Quick display: the common view controls, shared with the
            // Options > Display page via the `display` backend.
            Label { text: qsTr("Quick display"); font.bold: true }
            CheckBox {
                text: qsTr("Follow own ship")
                checked: chart.followOwnShip
                onToggled: chart.followOwnShip = checked
            }
            CheckBox {
                text: qsTr("Compass window")
                checked: DisplayConfig.showCompass
                onToggled: DisplayConfig.showCompass = checked
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Orientation:") }
                RadioButton {
                    text: qsTr("N-Up")
                    checked: DisplayConfig.navMode === 0
                    onClicked: DisplayConfig.navMode = 0
                }
                RadioButton {
                    text: qsTr("C-Up")
                    checked: DisplayConfig.navMode === 1
                    onClicked: DisplayConfig.navMode = 1
                }
                RadioButton {
                    text: qsTr("H-Up")
                    checked: DisplayConfig.navMode === 2
                    onClicked: DisplayConfig.navMode = 2
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    // --- Route & mark manager: a left-edge drawer of route tiles (P3.7).
    //     Non-modal + undimmed so the chart stays live behind it; a tile shows
    //     the route name + stats, click zooms to its extent, and the ... menu
    //     holds rename (inline) / duplicate / reverse / delete.
    Drawer {
        id: routeDrawer
        edge: Qt.LeftEdge
        width: 340
        height: root.height
        modal: false
        dim: false

        readonly property var rl: chart.routeList

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("Routes & marks")
                    font.pointSize: 14; font.bold: true
                    Layout.fillWidth: true
                }
                ToolButton { text: "✕"; onClicked: routeDrawer.close() }
            }

            TabBar {
                id: drawerTabs
                Layout.fillWidth: true
                TabButton { text: qsTr("Routes") }
                TabButton { text: qsTr("Marks") }
                TabButton { text: qsTr("Tracks") }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: drawerTabs.currentIndex

                // --- Routes page ---------------------------------------------
                ColumnLayout {
                    spacing: 6
                    Label {
                        text: qsTr("Routes (") +
                              (routeDrawer.rl ? routeDrawer.rl.routes.length : 0) + ")"
                        font.pointSize: 11; color: "#9aa0a6"
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: routeDrawer.rl ? routeDrawer.rl.routes : []
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            property bool editing: false
                            // Is this route the one currently being followed?
                            // (P3.16) -- live via the follower's signal.
                            property bool isActiveRoute:
                                chart.routeFollower.activeRouteGuid.length > 0 &&
                                chart.routeFollower.activeRouteGuid === modelData.guid
                            width: ListView.view.width
                            height: tileCol.implicitHeight + 16
                            radius: 6
                            color: tileMouse.containsMouse ? "#26ffffff" : "#14ffffff"
                            border.color: isActiveRoute ? "#ff5a28" : "#33808080"
                            border.width: isActiveRoute ? 2 : 1
                            MouseArea {
                                id: tileMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: chart.showRoute(index)
                            }
                            ColumnLayout {
                                id: tileCol
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    ToolButton {
                                        text: "👁"
                                        font.pointSize: 13
                                        implicitWidth: 34
                                        opacity: (chart.routeVisibilityRevision,
                                                  chart.routeVisible(index)) ? 1.0 : 0.3
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Show / hide this route")
                                        onClicked: chart.setRouteVisible(
                                                       index, !chart.routeVisible(index))
                                    }
                                    Label {
                                        visible: !editing
                                        text: modelData.name.length > 0 ? modelData.name
                                                                        : qsTr("(unnamed)")
                                        font.pointSize: 13; font.bold: true
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                    TextField {
                                        id: nameEdit
                                        visible: editing
                                        Layout.fillWidth: true
                                        selectByMouse: true
                                        font.pointSize: 13
                                        onAccepted: {
                                            chart.renameRoute(index, text)
                                            editing = false
                                        }
                                        Keys.onEscapePressed: editing = false
                                        onActiveFocusChanged:
                                            if (!activeFocus && editing) {
                                                chart.renameRoute(index, text)
                                                editing = false
                                            }
                                    }
                                    ToolButton {
                                        text: "⋯"
                                        font.pointSize: 15
                                        onClicked: tileMenu.open()
                                        Menu {
                                            id: tileMenu
                                            MenuItem {
                                                text: isActiveRoute ? qsTr("Deactivate")
                                                                    : qsTr("Activate")
                                                onTriggered: isActiveRoute
                                                    ? chart.deactivateRoute()
                                                    : chart.activateRoute(index)
                                            }
                                            MenuItem {
                                                text: qsTr("Skip waypoint")
                                                enabled: isActiveRoute
                                                onTriggered: chart.skipWaypoint()
                                            }
                                            MenuSeparator {}
                                            MenuItem {
                                                text: qsTr("Edit")
                                                onTriggered: chart.editRoute(index)
                                            }
                                            MenuItem {
                                                text: qsTr("Details…")
                                                onTriggered: routeDetailsDialog.openFor(
                                                    index, modelData.name)
                                            }
                                            MenuItem {
                                                text: qsTr("Duplicate")
                                                onTriggered: chart.duplicateRoute(index)
                                            }
                                            MenuItem {
                                                text: qsTr("Reverse")
                                                onTriggered: chart.reverseRoute(index)
                                            }
                                            MenuSeparator {}
                                            MenuItem {
                                                text: qsTr("Delete")
                                                onTriggered: chart.deleteRoute(index)
                                            }
                                        }
                                    }
                                }
                                Label {
                                    text: modelData.lengthNm.toFixed(1) + qsTr(" NM · ") +
                                          Math.max(0, modelData.points - 1) + qsTr(" legs")
                                    color: "#9aa0a6"; font.pointSize: 10
                                }
                            }
                        }
                    }
                }

                // --- Marks page ----------------------------------------------
                ColumnLayout {
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Marks (") +
                                  (routeDrawer.rl ? routeDrawer.rl.waypoints.length : 0) + ")"
                            font.pointSize: 11; color: "#9aa0a6"
                            Layout.fillWidth: true
                        }
                        Label { text: qsTr("Sort"); font.pointSize: 10; color: "#9aa0a6" }
                        ComboBox {
                            model: [qsTr("Recent"), qsTr("Nearest")]
                            currentIndex: routeDrawer.rl ? routeDrawer.rl.markSortMode : 0
                            onActivated: if (routeDrawer.rl) routeDrawer.rl.markSortMode = currentIndex
                            implicitWidth: 120
                        }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: routeDrawer.rl ? routeDrawer.rl.waypoints : []
                        delegate: Rectangle {
                            required property var modelData
                            property bool editing: false
                            width: ListView.view.width
                            height: mtileCol.implicitHeight + 16
                            radius: 6
                            color: mtileMouse.containsMouse ? "#26ffffff" : "#14ffffff"
                            border.color: "#33808080"; border.width: 1
                            MouseArea {
                                id: mtileMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: chart.showMark(modelData.guid)
                            }
                            ColumnLayout {
                                id: mtileCol
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    ToolButton {
                                        text: "👁"
                                        font.pointSize: 13
                                        implicitWidth: 34
                                        opacity: modelData.visible ? 1.0 : 0.3
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Show / hide this mark")
                                        onClicked: chart.setMarkVisible(
                                                       modelData.guid, !modelData.visible)
                                    }
                                    Image {
                                        source: "image://wpicon/" + modelData.icon
                                        Layout.preferredWidth: 24
                                        Layout.preferredHeight: 22
                                        sourceSize.height: 22
                                        fillMode: Image.PreserveAspectFit
                                    }
                                    Label {
                                        visible: !editing
                                        text: modelData.name
                                        font.pointSize: 13; font.bold: true
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                    TextField {
                                        id: mNameEdit
                                        visible: editing
                                        Layout.fillWidth: true
                                        selectByMouse: true
                                        font.pointSize: 13
                                        onAccepted: {
                                            chart.renameMark(modelData.guid, text)
                                            editing = false
                                        }
                                        Keys.onEscapePressed: editing = false
                                        onActiveFocusChanged:
                                            if (!activeFocus && editing) {
                                                chart.renameMark(modelData.guid, text)
                                                editing = false
                                            }
                                    }
                                    ToolButton {
                                        text: "⋯"
                                        font.pointSize: 15
                                        onClicked: mtileMenu.open()
                                        Menu {
                                            id: mtileMenu
                                            MenuItem {
                                                text: qsTr("Edit")
                                                onTriggered: markEditor.openForEdit(
                                                    modelData.guid, modelData.name,
                                                    modelData.comment, modelData.icon)
                                            }
                                            MenuItem {
                                                text: qsTr("Rename")
                                                onTriggered: {
                                                    mNameEdit.text = modelData.name
                                                    editing = true
                                                    mNameEdit.forceActiveFocus()
                                                    mNameEdit.selectAll()
                                                }
                                            }
                                            MenuSeparator {}
                                            MenuItem {
                                                text: qsTr("Delete")
                                                onTriggered: chart.deleteMark(modelData.guid)
                                            }
                                        }
                                    }
                                }
                                Label {
                                    visible: modelData.comment.length > 0
                                    text: modelData.comment
                                    color: "#9aa0a6"; font.pointSize: 10
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2; elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: modelData.rangeNm >= 0
                                          ? modelData.rangeNm.toFixed(1) + qsTr(" NM away")
                                          : qsTr("position unknown")
                                    color: "#9aa0a6"; font.pointSize: 9
                                }
                            }
                        }
                    }
                }

                // --- Tracks page ---------------------------------------------
                ColumnLayout {
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Button {
                            text: chart.trackRecording ? qsTr("Stop") : qsTr("Start")
                            onClicked: chart.trackRecording = !chart.trackRecording
                        }
                        Button {
                            text: qsTr("Reset")
                            enabled: chart.trackRecording
                            onClicked: chart.resetTrack()
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: qsTr("(") +
                                  (routeDrawer.rl ? routeDrawer.rl.tracks.length : 0) + ")"
                            font.pointSize: 11; color: "#9aa0a6"
                        }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: routeDrawer.rl ? routeDrawer.rl.tracks : []
                        delegate: Rectangle {
                            required property var modelData
                            property bool editing: false
                            width: ListView.view.width
                            height: ttileCol.implicitHeight + 16
                            radius: 6
                            color: ttileMouse.containsMouse ? "#26ffffff" : "#14ffffff"
                            border.color: modelData.active ? "#7f9b00c8" : "#33808080"
                            border.width: modelData.active ? 2 : 1
                            MouseArea {
                                id: ttileMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: chart.showTrack(modelData.guid)
                            }
                            ColumnLayout {
                                id: ttileCol
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    ToolButton {
                                        text: "👁"
                                        font.pointSize: 13
                                        implicitWidth: 34
                                        opacity: modelData.visible ? 1.0 : 0.3
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Show / hide this track")
                                        onClicked: chart.setTrackVisible(
                                                       modelData.guid, !modelData.visible)
                                    }
                                    Label {
                                        visible: !editing
                                        text: modelData.name +
                                              (modelData.active ? qsTr("  ● REC") : "")
                                        font.pointSize: 13; font.bold: true
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                        color: modelData.active ? "#d070ff"
                                                                : palette.windowText
                                    }
                                    TextField {
                                        id: tNameEdit
                                        visible: editing
                                        Layout.fillWidth: true
                                        selectByMouse: true
                                        font.pointSize: 13
                                        onAccepted: {
                                            chart.renameTrack(modelData.guid, text)
                                            editing = false
                                        }
                                        Keys.onEscapePressed: editing = false
                                        onActiveFocusChanged:
                                            if (!activeFocus && editing) {
                                                chart.renameTrack(modelData.guid, text)
                                                editing = false
                                            }
                                    }
                                    ToolButton {
                                        text: "⋯"
                                        font.pointSize: 15
                                        onClicked: ttileMenu.open()
                                        Menu {
                                            id: ttileMenu
                                            MenuItem {
                                                text: qsTr("Rename")
                                                onTriggered: {
                                                    tNameEdit.text = modelData.name
                                                    editing = true
                                                    tNameEdit.forceActiveFocus()
                                                    tNameEdit.selectAll()
                                                }
                                            }
                                            MenuSeparator {}
                                            MenuItem {
                                                text: qsTr("Delete")
                                                onTriggered: chart.deleteTrack(modelData.guid)
                                            }
                                        }
                                    }
                                }
                                Label {
                                    text: modelData.lengthNm.toFixed(1) + qsTr(" NM")
                                    color: "#9aa0a6"; font.pointSize: 10
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Mark editor (P3.7): one dialog for "New mark" (dropped via the chart
    //     right-click) and "Edit mark" (the drawer tile's Edit). Captures name,
    //     comment, and a visual icon pick (images via the wpicon provider).
    Dialog {
        id: markEditor
        title: editMode ? qsTr("Edit mark") : qsTr("New mark")
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel

        property bool editMode: false
        property string guid: ""
        property string iconName: "triangle"

        function openNew() {
            editMode = false; guid = "";
            markNameField.text = ""; markCommentField.text = "";
            // Default to the configured mark icon (Options > User Interface >
            // Routes & Marks).
            iconName = RouteDefaultsConfig.waypointIcon || "triangle";
            open()
        }
        function openForEdit(g, nm, cm, ic) {
            editMode = true; guid = g;
            markNameField.text = nm; markCommentField.text = cm;
            iconName = ic.length > 0 ? ic : "triangle";
            open()
        }
        onAccepted: {
            if (editMode) {
                chart.renameMark(guid, markNameField.text)
                chart.setMarkComment(guid, markCommentField.text)
                chart.setMarkIcon(guid, iconName)
            } else {
                chart.dropMarkHere(markNameField.text, markCommentField.text,
                                   iconName)
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                visible: !markEditor.editMode
                text: qsTr("At ") + chart.markDropLat().toFixed(4) + ", " +
                      chart.markDropLon().toFixed(4)
                color: "#9aa0a6"; font.pointSize: 10
            }
            TextField {
                id: markNameField
                placeholderText: qsTr("Name")
                Layout.fillWidth: true
                selectByMouse: true
            }
            TextField {
                id: markCommentField
                placeholderText: qsTr("Comment")
                Layout.fillWidth: true
                selectByMouse: true
            }
            Label { text: qsTr("Icon"); font.pointSize: 10; color: "#9aa0a6" }
            GridView {
                Layout.fillWidth: true
                Layout.preferredHeight: 132
                clip: true
                cellWidth: 44; cellHeight: 44
                model: chart.markIconNames()
                delegate: Rectangle {
                    required property var modelData
                    width: 42; height: 42; radius: 4
                    color: modelData === markEditor.iconName ? "#553b82f6"
                                                             : "transparent"
                    border.color: modelData === markEditor.iconName ? "#3b82f6"
                                                                    : "#33808080"
                    Image {
                        anchors.centerIn: parent
                        source: "image://wpicon/" + modelData
                        sourceSize.height: 28
                        fillMode: Image.PreserveAspectFit
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: markEditor.iconName = modelData
                    }
                }
            }
        }
    }

    // --- Route details: the route name + a per-route mark icon for its points
    //     (the dot is the default; pick an icon to override all the route's
    //     points). Opened from the routes drawer ⋯ menu.
    Window {
        id: routeDetailsDialog
        title: qsTr("Route details")
        flags: Qt.Dialog
        modality: Qt.ApplicationModal
        width: 420
        height: 360
        color: palette.window

        property int routeIndex: -1
        property string iconName: ""

        // Build the icon model by hand: "" (plain dot) then the catalogue.
        // [].concat(aQStringList) appends the list as a single element rather
        // than spreading it, which broke the grid.
        function iconModel() {
            var names = chart.markIconNames()
            var out = [""]
            for (var i = 0; i < names.length; ++i) out.push(names[i])
            return out
        }
        function openFor(idx, nm) {
            routeIndex = idx
            routeNameField.text = nm
            iconName = chart.routePointIcon(idx)
            show(); raise(); requestActivate()
            routeNameField.forceActiveFocus()
        }
        function apply() {
            if (routeIndex < 0) return
            chart.renameRoute(routeIndex, routeNameField.text)
            chart.setRoutePointIcon(routeIndex, iconName)
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 14

            GridLayout {
                columns: 2
                columnSpacing: 14
                rowSpacing: 10
                Layout.fillWidth: true

                Label { text: qsTr("Name:"); Layout.alignment: Qt.AlignRight }
                TextField {
                    id: routeNameField
                    Layout.fillWidth: true
                    selectByMouse: true
                }

                Label {
                    text: qsTr("Point icon:")
                    Layout.alignment: Qt.AlignRight | Qt.AlignTop
                    Layout.topMargin: 6
                }
                Frame {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 4
                    GridView {
                        id: iconGrid
                        anchors.fill: parent
                        clip: true
                        cellWidth: 46; cellHeight: 46
                        model: routeDetailsDialog.iconModel()
                        delegate: ItemDelegate {
                            required property var modelData
                            width: 44; height: 44
                            padding: 0
                            highlighted: modelData === routeDetailsDialog.iconName
                            onClicked: routeDetailsDialog.iconName = modelData
                            ToolTip.visible: hovered && modelData.length > 0
                            ToolTip.text: modelData
                            contentItem: Item {
                                Image {
                                    anchors.centerIn: parent
                                    visible: modelData.length > 0
                                    source: modelData.length > 0
                                            ? "image://wpicon/" + modelData : ""
                                    sourceSize.height: 30
                                    fillMode: Image.PreserveAspectFit
                                }
                                // Empty entry = the plain "dot" default.
                                Rectangle {
                                    visible: modelData.length === 0
                                    anchors.centerIn: parent
                                    width: 9; height: 9; radius: 4.5
                                    color: palette.windowText
                                }
                            }
                        }
                    }
                }
            }

            DialogButtonBox {
                Layout.fillWidth: true
                standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
                onAccepted: { routeDetailsDialog.apply(); routeDetailsDialog.close() }
                onRejected: routeDetailsDialog.close()
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

    // --- Options: the tabbed settings window, following the macOS settings
    //     HIG: a centred preference-style tab toolbar; standard (not touch)
    //     native controls -- checkboxes/radio buttons, no enlarged sizes;
    //     20pt margins; modeless, close via the window control; fixed size,
    //     title shows the current pane. Other tabs are placeholders for now.
    Window {
        id: optionsWindow
        flags: Qt.Dialog
        // macOS shows a wider window (sidebar + pane), like System Settings.
        // Other platforms get the compact top-tab layout.
        width: optionsWindow.useSidebar ? 720 : 540
        height: 520
        // Non-resizable, as macOS settings windows are.
        minimumWidth: width; maximumWidth: width
        minimumHeight: height; maximumHeight: height
        color: palette.window

        // --- Options framework ------------------------------------------
        // The pages are shared across platforms; only the navigation chrome
        // changes -- a left sidebar (macOS System-Settings style) vs. a top
        // tab bar (Windows/Linux). `currentPage` is the single source of
        // truth that both chromes drive and the content StackLayout follows.
        readonly property bool useSidebar: Qt.platform.os === "osx"
        property int currentPage: 0
        // Page metadata for the sidebar (title + glyph + accent colour, in the
        // spirit of macOS System Settings' coloured icons). Order matches the
        // content StackLayout below.
        readonly property var pages: [
            { title: qsTr("Display"),     glyph: "▦", accent: "#3478f6" },
            { title: qsTr("Charts"),      glyph: "◈", accent: "#34c759" },
            { title: qsTr("Connections"), glyph: "⇄", accent: "#ff9500" },
            { title: qsTr("Ships"),       glyph: "⚓", accent: "#30b0c7" },
            { title: qsTr("User Interface"), glyph: "▤", accent: "#ff2d55" },
            { title: qsTr("Plugins"),     glyph: "▣", accent: "#af52de" }
        ]
        title: qsTr("Options") + " — " + pages[currentPage].title

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Top tab bar -- the Windows/Linux chrome. Hidden on macOS.
            TabBar {
                id: optTabs
                visible: !optionsWindow.useSidebar
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 6
                currentIndex: optionsWindow.currentPage
                onCurrentIndexChanged: optionsWindow.currentPage = currentIndex
                Repeater {
                    model: optionsWindow.pages
                    TabButton {
                        required property var modelData
                        text: modelData.title
                        width: implicitWidth
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Left sidebar -- the macOS chrome. Hidden elsewhere.
                Rectangle {
                    visible: optionsWindow.useSidebar
                    Layout.fillHeight: true
                    Layout.preferredWidth: 200
                    // Subtle inset-sidebar tone, like System Settings.
                    color: Qt.darker(palette.window, 1.04)
                    border.width: 0

                    ListView {
                        id: sidebar
                        anchors.fill: parent
                        anchors.topMargin: 12
                        anchors.bottomMargin: 12
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        clip: true
                        interactive: false
                        spacing: 2
                        model: optionsWindow.pages
                        currentIndex: optionsWindow.currentPage
                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            height: 34
                            onClicked: optionsWindow.currentPage = index
                            background: Rectangle {
                                radius: 6
                                color: index === optionsWindow.currentPage
                                       ? palette.highlight
                                       : (hovered ? Qt.rgba(0.5, 0.5, 0.5, 0.12)
                                                  : "transparent")
                            }
                            contentItem: RowLayout {
                                spacing: 9
                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    width: 22; height: 22; radius: 5
                                    color: modelData.accent
                                    Label {
                                        anchors.centerIn: parent
                                        text: modelData.glyph
                                        color: "white"
                                        font.pointSize: 12
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: modelData.title
                                    elide: Text.ElideRight
                                    color: index === optionsWindow.currentPage
                                           ? palette.highlightedText
                                           : palette.windowText
                                }
                            }
                        }
                    }
                }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: optionsWindow.currentPage

                // --- Display: General / Units / Advanced sub-tabs, mirroring
                //     the wx Options > Display notebook. All controls bind to
                //     the shared `display` (DisplayConfig) backend except the
                //     Qt-specific demo/debug toggles and follow (on `chart`).
                Item {
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 12

                        TabBar {
                            id: displaySubTabs
                            Layout.fillWidth: true
                            TabButton { text: qsTr("General") }
                            TabButton { text: qsTr("Units") }
                            TabButton { text: qsTr("Advanced") }
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: displaySubTabs.currentIndex

                            // --- General ---
                            ScrollView {
                                id: genScroll
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                contentWidth: availableWidth
                                ColumnLayout {
                                    width: genScroll.availableWidth
                                    spacing: 8

                                    Label { text: qsTr("Navigation"); font.bold: true }
                                    RowLayout {
                                        Label { text: qsTr("Chart orientation:") }
                                        RadioButton {
                                            text: qsTr("North-Up")
                                            checked: DisplayConfig.navMode === 0
                                            onClicked: DisplayConfig.navMode = 0
                                        }
                                        RadioButton {
                                            text: qsTr("Course-Up")
                                            checked: DisplayConfig.navMode === 1
                                            onClicked: DisplayConfig.navMode = 1
                                        }
                                        RadioButton {
                                            text: qsTr("Head-Up")
                                            checked: DisplayConfig.navMode === 2
                                            onClicked: DisplayConfig.navMode = 2
                                        }
                                    }
                                    CheckBox {
                                        text: qsTr("Auto-follow own ship")
                                        checked: chart.followOwnShip
                                        onToggled: chart.followOwnShip = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Look ahead while following")
                                        checked: DisplayConfig.lookAhead
                                        onToggled: DisplayConfig.lookAhead = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Preserve scale on chart switch")
                                        checked: DisplayConfig.preserveScaleOnSwitch
                                        onToggled: DisplayConfig.preserveScaleOnSwitch = checked
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Display & controls"); font.bold: true }
                                    CheckBox {
                                        text: qsTr("Show compass / GPS window")
                                        checked: DisplayConfig.showCompass
                                        onToggled: DisplayConfig.showCompass = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Grey “no data” fill where no ENC coverage")
                                        checked: DisplayConfig.showNoData
                                        onToggled: DisplayConfig.showNoData = checked
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("ECDIS look: paint uncovered areas the S-52 no-data grey instead of the world basemap")
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: qsTr("Mouse-wheel zoom:") }
                                        Slider {
                                            Layout.fillWidth: true
                                            from: 1.1; to: 2.0; stepSize: 0.05
                                            value: DisplayConfig.wheelZoomFactor
                                            onMoved: DisplayConfig.wheelZoomFactor = value
                                        }
                                        Label {
                                            text: DisplayConfig.wheelZoomFactor.toFixed(2) + "×"
                                            font.family: "monospace"
                                        }
                                    }
                                    RowLayout {
                                        Label { text: qsTr("Time display:") }
                                        RadioButton {
                                            text: qsTr("UTC")
                                            checked: DisplayConfig.timeZone === 0
                                            onClicked: DisplayConfig.timeZone = 0
                                        }
                                        RadioButton {
                                            text: qsTr("Local")
                                            checked: DisplayConfig.timeZone === 1
                                            onClicked: DisplayConfig.timeZone = 1
                                        }
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Own-ship vectors"); font.bold: true }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8; rowSpacing: 8
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("COG/SOG predictor (min):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 1; to: 60
                                            value: Math.round(DisplayConfig.cogPredictorMinutes)
                                            onValueModified: DisplayConfig.cogPredictorMinutes = value
                                        }
                                        Label {
                                            text: qsTr("SOG/COG damping (s):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 30
                                            value: Math.round(DisplayConfig.sogCogDampingSeconds)
                                            onValueModified: DisplayConfig.sogCogDampingSeconds = value
                                        }
                                        Label {
                                            text: qsTr("Default boat speed (kn):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 1; to: 60
                                            value: Math.round(DisplayConfig.defaultBoatSpeed)
                                            onValueModified: DisplayConfig.defaultBoatSpeed = value
                                        }
                                        Label {
                                            text: qsTr("Current vector (min):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 1; to: 120
                                            value: Math.round(DisplayConfig.currentVectorMinutes)
                                            onValueModified: DisplayConfig.currentVectorMinutes = value
                                            ToolTip.visible: hovered
                                            ToolTip.text: qsTr("On-chart current arrows show the distance the current carries you in this many minutes, at chart scale")
                                        }
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Prototype (Qt-only)"); font.bold: true }
                                    CheckBox {
                                        text: qsTr("Demo nav data (Hakefjord replay)")
                                        checked: chart.demoMode
                                        onToggled: chart.demoMode = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Show debug overlay")
                                        checked: root.showDebug
                                        onToggled: root.showDebug = checked
                                    }
                                }
                            }

                            // --- Units ---
                            ScrollView {
                                id: unitsScroll
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                contentWidth: availableWidth
                                ColumnLayout {
                                    width: unitsScroll.availableWidth
                                    spacing: 8
                                    Label { text: qsTr("Units"); font.bold: true }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8; rowSpacing: 8
                                        Layout.fillWidth: true

                                        Label {
                                            text: qsTr("Distance:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Nautical miles"), qsTr("Kilometres"),
                                                    qsTr("Statute miles")]
                                            currentIndex: DisplayConfig.distanceUnit
                                            onActivated: DisplayConfig.distanceUnit = currentIndex
                                        }
                                        Label {
                                            text: qsTr("Speed:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Knots"), qsTr("km/h"), qsTr("mph")]
                                            currentIndex: DisplayConfig.speedUnit
                                            onActivated: DisplayConfig.speedUnit = currentIndex
                                        }
                                        Label {
                                            text: qsTr("Wind speed:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Knots"), qsTr("m/s"), qsTr("km/h"),
                                                    qsTr("mph")]
                                            currentIndex: DisplayConfig.windUnit
                                            onActivated: DisplayConfig.windUnit = currentIndex
                                        }
                                        Label {
                                            text: qsTr("Depth:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Metres"), qsTr("Feet"), qsTr("Fathoms")]
                                            currentIndex: DisplayConfig.depthUnit
                                            onActivated: DisplayConfig.depthUnit = currentIndex
                                        }
                                        Label {
                                            text: qsTr("Height:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Metres"), qsTr("Feet")]
                                            currentIndex: DisplayConfig.heightUnit
                                            onActivated: DisplayConfig.heightUnit = currentIndex
                                        }
                                        Label {
                                            text: qsTr("Temperature:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Celsius"), qsTr("Fahrenheit")]
                                            currentIndex: DisplayConfig.tempUnit
                                            onActivated: DisplayConfig.tempUnit = currentIndex
                                        }
                                        Label {
                                            text: qsTr("Lat/Lon format:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Degrees, decimal minutes"),
                                                    qsTr("Degrees, minutes, seconds"),
                                                    qsTr("Decimal degrees")]
                                            currentIndex: DisplayConfig.latLonFormat
                                            onActivated: DisplayConfig.latLonFormat = currentIndex
                                        }
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Bearings"); font.bold: true }
                                    CheckBox {
                                        text: qsTr("Show magnetic bearings")
                                        checked: DisplayConfig.showMagneticBearings
                                        onToggled: DisplayConfig.showMagneticBearings = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Use my own magnetic variation")
                                        enabled: DisplayConfig.showMagneticBearings
                                        checked: DisplayConfig.useUserMagVar
                                        onToggled: DisplayConfig.useUserMagVar = checked
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        enabled: DisplayConfig.showMagneticBearings && DisplayConfig.useUserMagVar
                                        Label { text: qsTr("Variation (°, E +):") }
                                        TextField {
                                            implicitWidth: 80
                                            text: DisplayConfig.userMagVar.toFixed(1)
                                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                                            validator: DoubleValidator { bottom: -180; top: 180; decimals: 1 }
                                            selectByMouse: true
                                            onEditingFinished: DisplayConfig.userMagVar = parseFloat(text)
                                        }
                                    }
                                }
                            }

                            // --- Advanced ---
                            ScrollView {
                                id: advScroll
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                contentWidth: availableWidth
                                ColumnLayout {
                                    width: advScroll.availableWidth
                                    spacing: 8
                                    Label { text: qsTr("Advanced"); font.bold: true }
                                    CheckBox {
                                        text: qsTr("De-skew raster charts")
                                        checked: DisplayConfig.deskewRaster
                                        onToggled: DisplayConfig.deskewRaster = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Responsive / touch sizing")
                                        checked: DisplayConfig.responsiveSizing
                                        onToggled: DisplayConfig.responsiveSizing = checked
                                    }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8; rowSpacing: 8
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("Course-up averaging (s):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 30
                                            value: Math.round(DisplayConfig.chartRotationAveraging)
                                            onValueModified: DisplayConfig.chartRotationAveraging = value
                                        }
                                        Label {
                                            text: qsTr("Screen width (mm, 0 = auto):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 1200
                                            value: Math.round(DisplayConfig.screenMmWidth)
                                            onValueModified: DisplayConfig.screenMmWidth = value
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // --- Charts: Chart Files / Vector Display / Groups / Tides
                //     sub-tabs. The display category + the four detail toggles
                //     the s52 provider honours are wired live (on `chart`); the
                //     extended vector options bind ChartConfig (persisted,
                //     pending provider support). File/group/tide management
                //     awaits a runtime chart-directory backend.
                Item {
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 12

                        TabBar {
                            id: chartsSubTabs
                            Layout.fillWidth: true
                            TabButton { text: qsTr("Chart Files") }
                            TabButton { text: qsTr("Vector Display") }
                            TabButton { text: qsTr("Groups") }
                            TabButton { text: qsTr("Tides") }
                            TabButton { text: qsTr("o-charts") }
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: chartsSubTabs.currentIndex

                            // --- Chart Files: runtime chart-directory manager ---
                            Item {
                                FolderDialog {
                                    id: chartFolderDialog
                                    title: qsTr("Add chart directory")
                                    onAccepted: chart.chartSource.addDirectory(
                                                    selectedFolder.toString())
                                }
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 8
                                    readonly property var cs: chart.chartSource

                                    Label { text: qsTr("Chart directories"); font.bold: true }
                                    ListView {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true
                                        model: parent.cs ? parent.cs.directories : []
                                        delegate: ItemDelegate {
                                            required property var modelData
                                            required property int index
                                            width: ListView.view.width
                                            contentItem: RowLayout {
                                                spacing: 8
                                                Label {
                                                    text: modelData
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideMiddle
                                                }
                                                ToolButton {
                                                    text: "✕"
                                                    onClicked: chart.chartSource.removeDirectory(index)
                                                }
                                            }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Button {
                                            text: qsTr("Add directory…")
                                            onClicked: chartFolderDialog.open()
                                        }
                                        Button {
                                            text: qsTr("Rescan")
                                            enabled: chart.chartSource &&
                                                     chart.chartSource.directories.length > 0
                                            onClicked: chart.chartSource.rescan()
                                        }
                                        BusyIndicator {
                                            running: chart.chartSource && chart.chartSource.scanning
                                            visible: running
                                            implicitWidth: 22; implicitHeight: 22
                                        }
                                        Item { Layout.fillWidth: true }
                                        Label {
                                            text: chart.chartSource ? chart.chartSource.status : ""
                                            color: palette.placeholderText
                                        }
                                    }
                                    Label {
                                        text: qsTr("Add folders of S-57 ENC (.000) cells. Charts are catalogued on scan and stream in as you zoom/pan.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 11
                                    }
                                }
                            }

                            // --- Vector Chart Display ---
                            ScrollView {
                                id: vchartScroll
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                contentWidth: availableWidth
                                ColumnLayout {
                                    width: vchartScroll.availableWidth
                                    spacing: 8

                                    Label { text: qsTr("Display category"); font.bold: true }
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

                                    MenuSeparator { Layout.fillWidth: true }

                                    // Same vector-detail checklist as the quick
                                    // pull-out drawer, via the shared
                                    // VectorDetailList component (single source
                                    // of truth -- the two surfaces stay in sync).
                                    VectorDetailList { Layout.fillWidth: true }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Quilt (Qt-specific)"); font.bold: true }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("Detail over-zoom (1 = at scale … 5 = max)")
                                            Layout.fillWidth: true
                                        }
                                        SpinBox {
                                            from: 1
                                            to: 5
                                            stepSize: 1
                                            editable: true
                                            value: chart.overzoomFactor
                                            onValueModified: chart.overzoomFactor = value
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("Hide un-SCAMIN'd detail beyond 1:")
                                            Layout.fillWidth: true
                                        }
                                        SpinBox {
                                            from: 5000
                                            to: 2000000
                                            stepSize: 5000
                                            editable: true
                                            value: chart.detailScale
                                            onValueModified: chart.detailScale = value
                                        }
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Style"); font.bold: true }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8; rowSpacing: 8
                                        Layout.fillWidth: true
                                        Label { text: qsTr("Graphics:"); Layout.alignment: Qt.AlignRight }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Paper chart"), qsTr("Simplified")]
                                            currentIndex: ChartConfig.graphicsStyle
                                            onActivated: ChartConfig.graphicsStyle = currentIndex
                                        }
                                        Label { text: qsTr("Boundaries:"); Layout.alignment: Qt.AlignRight }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Plain"), qsTr("Symbolised")]
                                            currentIndex: ChartConfig.boundaryStyle
                                            onActivated: ChartConfig.boundaryStyle = currentIndex
                                        }
                                        Label { text: qsTr("Colours:"); Layout.alignment: Qt.AlignRight }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Four colour"), qsTr("Two colour")]
                                            currentIndex: ChartConfig.colourCount
                                            onActivated: ChartConfig.colourCount = currentIndex
                                        }
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    // Contours are stored in metres but entered + shown in the
                                    // user's depth unit (ECDIS: the unit governs all depth I/O).
                                    Label { text: qsTr("Depth contours (%1)").arg(DisplayConfig.depthUnitLabel()); font.bold: true }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8; rowSpacing: 8
                                        Layout.fillWidth: true
                                        Label { text: qsTr("Shallow:"); Layout.alignment: Qt.AlignRight }
                                        SpinBox {
                                            from: 0
                                            to: { DisplayConfig.depthUnit; return Math.round(DisplayConfig.toUserDepth(50)); }
                                            value: { DisplayConfig.depthUnit;  // re-derive when the unit changes
                                                     return Math.round(DisplayConfig.toUserDepth(ChartConfig.shallowContour)); }
                                            onValueModified: ChartConfig.shallowContour = DisplayConfig.fromUserDepth(value)
                                        }
                                        Label { text: qsTr("Safety:"); Layout.alignment: Qt.AlignRight }
                                        SpinBox {
                                            from: 0
                                            to: { DisplayConfig.depthUnit; return Math.round(DisplayConfig.toUserDepth(50)); }
                                            value: { DisplayConfig.depthUnit;
                                                     return Math.round(DisplayConfig.toUserDepth(ChartConfig.safetyContour)); }
                                            onValueModified: ChartConfig.safetyContour = DisplayConfig.fromUserDepth(value)
                                        }
                                        Label { text: qsTr("Deep:"); Layout.alignment: Qt.AlignRight }
                                        SpinBox {
                                            from: 0
                                            to: { DisplayConfig.depthUnit; return Math.round(DisplayConfig.toUserDepth(200)); }
                                            value: { DisplayConfig.depthUnit;
                                                     return Math.round(DisplayConfig.toUserDepth(ChartConfig.deepContour)); }
                                            onValueModified: ChartConfig.deepContour = DisplayConfig.fromUserDepth(value)
                                        }
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("CM93"); font.bold: true }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: qsTr("Detail level:") }
                                        Slider {
                                            Layout.fillWidth: true
                                            from: -5; to: 5; stepSize: 1; snapMode: Slider.SnapAlways
                                            value: ChartConfig.cm93Detail
                                            onMoved: ChartConfig.cm93Detail = value
                                        }
                                        Label { text: ChartConfig.cm93Detail.toString(); font.family: "monospace" }
                                    }

                                    Label {
                                        text: qsTr("Display category and the four detail toggles above apply live. The remaining cartography options are saved and take effect once the S-52 provider exposes the matching viewing groups.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 11
                                    }
                                }
                            }

                            // --- Chart Groups ---
                            Item {
                                ColumnLayout {
                                    id: groupsTab
                                    anchors.fill: parent
                                    spacing: 8
                                    readonly property var gs: chart.chartSource
                                    property int editGroup: -1   // group being edited; -1 none

                                    // Directory list + membership for the group under edit. Reads
                                    // gs.groups so it re-evaluates on groupsChanged.
                                    property var memberModel: {
                                        var out = []
                                        if (!gs || editGroup < 0) return out
                                        var all = gs.groups
                                        var g = (editGroup < all.length) ? all[editGroup] : null
                                        var member = g ? g.dirs : []
                                        var dirs = gs.directories
                                        for (var i = 0; i < dirs.length; ++i)
                                            out.push({ dir: dirs[i],
                                                       member: member.indexOf(dirs[i]) >= 0 })
                                        return out
                                    }

                                    Label { text: qsTr("Chart groups"); font.bold: true }
                                    Label {
                                        text: qsTr("Define named subsets of your chart folders, then switch which set is active. \"All charts\" loads every folder.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 11
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Label { text: qsTr("Active group:") }
                                        ComboBox {
                                            id: activeGroupBox
                                            Layout.fillWidth: true
                                            model: {
                                                var names = [qsTr("All charts")]
                                                var g = groupsTab.gs ? groupsTab.gs.groups : []
                                                for (var i = 0; i < g.length; ++i) names.push(g[i].name)
                                                return names
                                            }
                                            currentIndex: groupsTab.gs ? groupsTab.gs.activeGroup + 1 : 0
                                            onActivated: if (groupsTab.gs)
                                                groupsTab.gs.activeGroup = currentIndex - 1
                                        }
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Groups"); font.bold: true }
                                    ListView {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 110
                                        clip: true
                                        model: groupsTab.gs ? groupsTab.gs.groups : []
                                        delegate: ItemDelegate {
                                            required property var modelData
                                            required property int index
                                            width: ListView.view.width
                                            highlighted: index === groupsTab.editGroup
                                            contentItem: RowLayout {
                                                spacing: 8
                                                Label {
                                                    text: modelData.name + "  (" + modelData.dirCount + ")"
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideRight
                                                }
                                                ToolButton {
                                                    text: "✎"
                                                    ToolTip.text: qsTr("Edit folders")
                                                    ToolTip.visible: hovered
                                                    onClicked: groupsTab.editGroup =
                                                        (groupsTab.editGroup === index ? -1 : index)
                                                }
                                                ToolButton {
                                                    text: "✕"
                                                    onClicked: {
                                                        if (groupsTab.editGroup === index)
                                                            groupsTab.editGroup = -1
                                                        groupsTab.gs.removeGroup(index)
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        TextField {
                                            id: newGroupField
                                            Layout.fillWidth: true
                                            placeholderText: qsTr("New group name")
                                            selectByMouse: true
                                            onAccepted: addGroupBtn.clicked()
                                        }
                                        Button {
                                            id: addGroupBtn
                                            text: qsTr("Add group")
                                            enabled: newGroupField.text.trim().length > 0
                                            onClicked: {
                                                var i = groupsTab.gs.addGroup(newGroupField.text)
                                                newGroupField.text = ""
                                                if (i >= 0) groupsTab.editGroup = i
                                            }
                                        }
                                    }

                                    MenuSeparator {
                                        Layout.fillWidth: true
                                        visible: groupsTab.editGroup >= 0
                                    }
                                    Label {
                                        visible: groupsTab.editGroup >= 0
                                        text: {
                                            var all = groupsTab.gs ? groupsTab.gs.groups : []
                                            var g = (groupsTab.editGroup >= 0
                                                     && groupsTab.editGroup < all.length)
                                                ? all[groupsTab.editGroup] : null
                                            return qsTr("Folders in ") + (g ? "“" + g.name + "”" : "")
                                        }
                                        font.bold: true
                                    }
                                    Label {
                                        visible: groupsTab.editGroup >= 0
                                               && groupsTab.memberModel.length === 0
                                        text: qsTr("No chart folders yet — add them under Chart Files.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 11
                                    }
                                    ListView {
                                        visible: groupsTab.editGroup >= 0
                                               && groupsTab.memberModel.length > 0
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 130
                                        clip: true
                                        model: groupsTab.memberModel
                                        delegate: CheckDelegate {
                                            required property var modelData
                                            width: ListView.view.width
                                            text: modelData.dir
                                            checked: modelData.member
                                            onToggled: groupsTab.gs.setDirInGroup(
                                                groupsTab.editGroup, modelData.dir, checked)
                                        }
                                    }

                                    Item { Layout.fillHeight: true }
                                }
                            }

                            // --- Tides & Currents data sets ---
                            Item {
                                FileDialog {
                                    id: tideFileDialog
                                    title: qsTr("Add tide / current data set")
                                    nameFilters: [qsTr("Harmonic data (*.tcd *.IDX *.idx)"),
                                                  qsTr("All files (*)")]
                                    onAccepted: tides.addSource(selectedFile.toString())
                                }
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 8
                                    Label { text: qsTr("Tide & current data sets"); font.bold: true }
                                    ListView {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true
                                        model: tides ? tides.dataSources : []
                                        delegate: ItemDelegate {
                                            required property var modelData
                                            required property int index
                                            width: ListView.view.width
                                            contentItem: RowLayout {
                                                spacing: 8
                                                Label {
                                                    text: modelData
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideMiddle
                                                }
                                                ToolButton {
                                                    text: "✕"
                                                    onClicked: tides.removeSource(index)
                                                }
                                            }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Button {
                                            text: qsTr("Add data set…")
                                            onClicked: tideFileDialog.open()
                                        }
                                        Item { Layout.fillWidth: true }
                                        Label {
                                            text: tides ? tides.status : ""
                                            color: palette.placeholderText
                                        }
                                    }
                                    Label {
                                        text: qsTr("Add harmonic data sets (a .tcd, or a HARMONIC .IDX). Stations are predicted by the built-in engine and drawn on the chart when Tides (≋) is on — scrub the timeline to see them change.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 11
                                    }
                                }
                            }

                            // --- o-charts (built-in, native -- not a plugin) ---
                            Item {
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 8
                                    Label { text: qsTr("o-charts (encrypted)"); font.bold: true }
                                    Label {
                                        text: OCharts.daemonAvailable
                                            ? qsTr("Decryption helper found: ") + OCharts.daemonVersion
                                            : qsTr("oexserverd decryption helper not found.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: OCharts.daemonAvailable ? "#34a853"
                                                                       : palette.placeholderText
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("System fingerprint"); font.bold: true }
                                    Label {
                                        text: qsTr("Generate this computer's fingerprint, then upload the .fpr file at o-charts.org to licence a chart set to this machine.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 11
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Button {
                                            text: qsTr("Generate fingerprint")
                                            enabled: OCharts.daemonAvailable && !OCharts.busy
                                            onClicked: OCharts.generateFingerprint()
                                        }
                                        BusyIndicator {
                                            running: OCharts.busy; visible: running
                                            implicitWidth: 22; implicitHeight: 22
                                        }
                                    }
                                    Label {
                                        text: OCharts.status
                                        visible: OCharts.status.length > 0
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        font.family: "monospace"; font.pointSize: 11
                                    }
                                    Item { Layout.fillHeight: true }
                                    Label {
                                        text: qsTr("Decryption + chart loading is built into the app (no plugin); install a licensed o-charts set, then add its folder under Chart Files.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 11
                                    }
                                }
                            }
                        }
                    }
                }

                // --- Connections: network data sources (#34) ---
                Item {
                    ColumnLayout {
                        id: connTab
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 8
                        readonly property var cm: chart.connections
                        property int editIndex: -1               // -1 = add mode
                        property var serialPorts: cm ? cm.availableSerialPorts() : []

                        function refreshPorts() {
                            serialPorts = cm ? cm.availableSerialPorts() : []
                        }
                        function buildRecord() {
                            var sp = ""
                            if (typeBox.currentIndex === 1 && serialBox.currentIndex >= 0
                                    && serialPorts.length > serialBox.currentIndex)
                                sp = serialPorts[serialBox.currentIndex].port
                            return {
                                type: typeBox.currentIndex,
                                netProto: netProtoBox.currentIndex,
                                address: addrField.text,
                                port: parseInt(portField.text) || 0,
                                serialPort: sp,
                                baud: parseInt(baudBox.currentText) || 4800,
                                dataProto: dataProtoBox.currentIndex,
                                ioSelect: ioBox.currentValue,
                                inFilterType: inFilterTypeBox.currentIndex,
                                inFilter: inFilterField.text,
                                outFilterType: outFilterTypeBox.currentIndex,
                                outFilter: outFilterField.text,
                                comment: commentField.text
                            }
                        }
                        function loadForm(c) {
                            typeBox.currentIndex = c.type || 0
                            netProtoBox.currentIndex = c.netProto || 0
                            addrField.text = c.address || ""
                            portField.text = c.port ? String(c.port) : ""
                            var idx = 0
                            for (var i = 0; i < serialPorts.length; ++i)
                                if (serialPorts[i].port === c.serialPort) { idx = i; break }
                            serialBox.currentIndex = idx
                            var bi = baudBox.find(String(c.baud || 4800))
                            baudBox.currentIndex = bi >= 0 ? bi : 0
                            dataProtoBox.currentIndex = c.dataProto || 0
                            ioBox.currentIndex = Math.max(0, ioBox.indexOfValue(c.ioSelect || 0))
                            inFilterTypeBox.currentIndex = c.inFilterType || 0
                            inFilterField.text = (c.inFilter || []).join(", ")
                            outFilterTypeBox.currentIndex = c.outFilterType || 0
                            outFilterField.text = (c.outFilter || []).join(", ")
                            commentField.text = c.comment || ""
                        }
                        function clearForm() {
                            editIndex = -1
                            typeBox.currentIndex = 0
                            netProtoBox.currentIndex = 0
                            addrField.text = ""
                            portField.text = ""
                            serialBox.currentIndex = 0
                            var bi = baudBox.find("4800")
                            baudBox.currentIndex = bi >= 0 ? bi : 0
                            dataProtoBox.currentIndex = 0
                            ioBox.currentIndex = 0
                            inFilterTypeBox.currentIndex = 0
                            inFilterField.text = ""
                            outFilterTypeBox.currentIndex = 0
                            outFilterField.text = ""
                            commentField.text = ""
                        }

                        Label { text: qsTr("Data connections"); font.bold: true }

                        ListView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 150
                            clip: true
                            model: connTab.cm ? connTab.cm.connections : []
                            delegate: ItemDelegate {
                                required property var modelData
                                required property int index
                                width: ListView.view.width
                                contentItem: RowLayout {
                                    spacing: 8
                                    CheckBox {
                                        checked: modelData.enabled
                                        onToggled: chart.connections.setEnabled(index, checked)
                                    }
                                    Label {
                                        text: modelData.summary
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                    ToolButton {
                                        text: "✎"
                                        ToolTip.text: qsTr("Edit")
                                        ToolTip.visible: hovered
                                        onClicked: {
                                            connTab.editIndex = index
                                            connTab.loadForm(chart.connections.connectionAt(index))
                                        }
                                    }
                                    ToolButton {
                                        text: "✕"
                                        onClicked: chart.connections.removeConnection(index)
                                    }
                                }
                            }
                        }

                        MenuSeparator { Layout.fillWidth: true }

                        Label {
                            text: connTab.editIndex >= 0 ? qsTr("Edit connection")
                                                         : qsTr("Add connection")
                            font.bold: true
                        }
                        GridLayout {
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8
                            Layout.fillWidth: true

                            Label {
                                text: qsTr("Type:")
                                Layout.alignment: Qt.AlignRight
                            }
                            ComboBox {
                                id: typeBox
                                Layout.fillWidth: true
                                model: ["Network", "Serial"]
                            }

                            // --- Network-only rows ---
                            Label {
                                text: qsTr("Transport:")
                                visible: typeBox.currentIndex === 0
                                Layout.alignment: Qt.AlignRight
                            }
                            ComboBox {
                                id: netProtoBox
                                visible: typeBox.currentIndex === 0
                                Layout.fillWidth: true
                                model: ["TCP", "UDP"]
                            }
                            Label {
                                text: qsTr("Address / host:")
                                visible: typeBox.currentIndex === 0
                                Layout.alignment: Qt.AlignRight
                            }
                            TextField {
                                id: addrField
                                visible: typeBox.currentIndex === 0
                                Layout.fillWidth: true
                                placeholderText: qsTr("e.g. 192.168.1.10 (TCP) or 0.0.0.0 (UDP listen)")
                                selectByMouse: true
                            }
                            Label {
                                text: qsTr("Port:")
                                visible: typeBox.currentIndex === 0
                                Layout.alignment: Qt.AlignRight
                            }
                            TextField {
                                id: portField
                                visible: typeBox.currentIndex === 0
                                Layout.fillWidth: true
                                placeholderText: qsTr("e.g. 2000 / 60001")
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: IntValidator { bottom: 1; top: 65535 }
                                selectByMouse: true
                            }

                            // --- Serial-only rows ---
                            Label {
                                text: qsTr("Serial port:")
                                visible: typeBox.currentIndex === 1
                                Layout.alignment: Qt.AlignRight
                            }
                            RowLayout {
                                visible: typeBox.currentIndex === 1
                                Layout.fillWidth: true
                                spacing: 6
                                ComboBox {
                                    id: serialBox
                                    Layout.fillWidth: true
                                    model: connTab.serialPorts
                                    textRole: "description"
                                    displayText: connTab.serialPorts.length === 0
                                        ? qsTr("(no serial ports found)") : currentText
                                }
                                ToolButton {
                                    text: "⟳"
                                    ToolTip.text: qsTr("Rescan ports")
                                    ToolTip.visible: hovered
                                    onClicked: connTab.refreshPorts()
                                }
                            }
                            Label {
                                text: qsTr("Baud:")
                                visible: typeBox.currentIndex === 1
                                Layout.alignment: Qt.AlignRight
                            }
                            ComboBox {
                                id: baudBox
                                visible: typeBox.currentIndex === 1
                                Layout.fillWidth: true
                                model: connTab.cm ? connTab.cm.baudRates() : [4800]
                                Component.onCompleted: {
                                    var bi = find("4800"); currentIndex = bi >= 0 ? bi : 0
                                }
                            }

                            // --- Common rows ---
                            Label {
                                text: qsTr("Data protocol:")
                                Layout.alignment: Qt.AlignRight
                            }
                            ComboBox {
                                id: dataProtoBox
                                Layout.fillWidth: true
                                model: ["NMEA 0183", "NMEA 2000"]
                            }
                            Label {
                                text: qsTr("Direction:")
                                Layout.alignment: Qt.AlignRight
                            }
                            ComboBox {
                                id: ioBox
                                Layout.fillWidth: true
                                textRole: "text"
                                valueRole: "value"
                                model: [{ text: qsTr("Input"), value: 0 },
                                        { text: qsTr("Input + Output"), value: 1 },
                                        { text: qsTr("Output"), value: 2 }]
                            }
                            Label {
                                text: qsTr("Input filter:")
                                Layout.alignment: Qt.AlignRight
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                ComboBox {
                                    id: inFilterTypeBox
                                    model: [qsTr("Accept"), qsTr("Ignore")]
                                    Layout.preferredWidth: 110
                                }
                                TextField {
                                    id: inFilterField
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("sentences, e.g. GGA, RMC (blank = all)")
                                    selectByMouse: true
                                }
                            }
                            Label {
                                text: qsTr("Output filter:")
                                Layout.alignment: Qt.AlignRight
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                ComboBox {
                                    id: outFilterTypeBox
                                    model: [qsTr("Accept"), qsTr("Ignore")]
                                    Layout.preferredWidth: 110
                                }
                                TextField {
                                    id: outFilterField
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("sentences (blank = all)")
                                    selectByMouse: true
                                }
                            }
                            Label {
                                text: qsTr("Comment:")
                                Layout.alignment: Qt.AlignRight
                            }
                            TextField {
                                id: commentField
                                Layout.fillWidth: true
                                placeholderText: qsTr("optional label")
                                selectByMouse: true
                            }

                            Item {}  // spacer in label column
                            RowLayout {
                                spacing: 8
                                Button {
                                    text: connTab.editIndex >= 0 ? qsTr("Save") : qsTr("Add")
                                    enabled: typeBox.currentIndex === 1
                                        ? connTab.serialPorts.length > 0
                                        : (addrField.text.length > 0 && portField.text.length > 0)
                                    onClicked: {
                                        if (connTab.editIndex >= 0)
                                            chart.connections.updateConnection(
                                                connTab.editIndex, connTab.buildRecord())
                                        else
                                            chart.connections.addConnection(connTab.buildRecord())
                                        connTab.clearForm()
                                    }
                                }
                                Button {
                                    text: qsTr("Cancel")
                                    visible: connTab.editIndex >= 0
                                    onClicked: connTab.clearForm()
                                }
                            }
                        }
                        Label {
                            text: qsTr("Enabling a connection opens the transport and switches to live data. Serial and TCP/UDP (NMEA 0183 / NMEA 2000) are supported.")
                            wrapMode: Text.Wrap; Layout.fillWidth: true
                            color: palette.placeholderText; font.pointSize: 11
                        }
                        Item { Layout.fillHeight: true }
                    }
                }

                // --- Ships: own-ship identity + AIS sub-screens ---
                // Demonstrates a page with internal sub-screens (the
                // "General -> About" pattern). The sub-tabs live inside the
                // page, so they travel with it across both chromes.
                Item {
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 12

                        TabBar {
                            id: shipsSubTabs
                            Layout.fillWidth: true
                            TabButton { text: qsTr("Own ship") }
                            TabButton { text: qsTr("AIS Targets") }
                            TabButton { text: qsTr("MMSI") }
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: shipsSubTabs.currentIndex

                            // --- Own ship: identity + display attributes. ---
                            ScrollView {
                                id: ownShipScroll
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                contentWidth: availableWidth
                                ColumnLayout {
                                    width: ownShipScroll.availableWidth
                                    spacing: 8
                                    Label { text: qsTr("Vessel identity"); font.bold: true }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8
                                        rowSpacing: 8
                                        Layout.fillWidth: true

                                        Label {
                                            text: qsTr("Vessel name:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: OwnShipConfig.vesselName
                                            placeholderText: qsTr("e.g. Serenity")
                                            selectByMouse: true
                                            onEditingFinished: OwnShipConfig.vesselName = text
                                        }
                                        Label {
                                            text: qsTr("MMSI:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: OwnShipConfig.mmsi
                                            placeholderText: qsTr("nine digits")
                                            inputMethodHints: Qt.ImhDigitsOnly
                                            maximumLength: 9
                                            validator: RegularExpressionValidator {
                                                regularExpression: /[0-9]{0,9}/
                                            }
                                            selectByMouse: true
                                            onEditingFinished: OwnShipConfig.mmsi = text
                                        }
                                    }
                                    Label {
                                        text: qsTr("Your own MMSI is hidden from the AIS display — we already plot your position from the GPS fix.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 11
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Display"); font.bold: true }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8; rowSpacing: 8
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("Ship icon:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Default symbol"),
                                                    qsTr("Real-scale bitmap"),
                                                    qsTr("Real-scale vector")]
                                            currentIndex: OwnShipConfig.iconType
                                            onActivated: OwnShipConfig.iconType = currentIndex
                                        }
                                        Label {
                                            text: qsTr("Length overall (m):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 500
                                            value: Math.round(OwnShipConfig.loa)
                                            onValueModified: OwnShipConfig.loa = value
                                        }
                                        Label {
                                            text: qsTr("Beam (m):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 100
                                            value: Math.round(OwnShipConfig.beam)
                                            onValueModified: OwnShipConfig.beam = value
                                        }
                                        Label {
                                            text: qsTr("GPS offset from bow (m):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 500
                                            value: Math.round(OwnShipConfig.gpsOffsetY)
                                            onValueModified: OwnShipConfig.gpsOffsetY = value
                                        }
                                        Label {
                                            text: qsTr("GPS offset from port (m):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 100
                                            value: Math.round(OwnShipConfig.gpsOffsetX)
                                            onValueModified: OwnShipConfig.gpsOffsetX = value
                                        }
                                        Label {
                                            text: qsTr("Min screen size (mm):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 100
                                            value: Math.round(OwnShipConfig.minScreenSize)
                                            onValueModified: OwnShipConfig.minScreenSize = value
                                        }
                                        Label {
                                            // Stored in metres; entered + shown in the user's depth unit.
                                            text: qsTr("Safety depth (%1):").arg(DisplayConfig.depthUnitLabel())
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0
                                            to: { DisplayConfig.depthUnit; return Math.round(DisplayConfig.toUserDepth(100)); }
                                            value: { DisplayConfig.depthUnit;  // re-derive when the unit changes
                                                     return Math.round(DisplayConfig.toUserDepth(OwnShipConfig.safetyDepth)); }
                                            onValueModified: OwnShipConfig.safetyDepth = DisplayConfig.fromUserDepth(value)
                                            ToolTip.visible: hovered
                                            ToolTip.text: qsTr("ENC soundings at or shallower than this are shown bold")
                                        }
                                    }
                                    CheckBox {
                                        text: qsTr("Show direction to active waypoint")
                                        checked: OwnShipConfig.showWaypointDirection
                                        onToggled: OwnShipConfig.showWaypointDirection = checked
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Range rings"); font.bold: true }
                                    CheckBox {
                                        text: qsTr("Show range rings")
                                        checked: OwnShipConfig.showRangeRings
                                        onToggled: OwnShipConfig.showRangeRings = checked
                                    }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8; rowSpacing: 8
                                        Layout.fillWidth: true
                                        enabled: OwnShipConfig.showRangeRings
                                        Label {
                                            text: qsTr("Number of rings:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 10
                                            value: OwnShipConfig.ringCount
                                            onValueModified: OwnShipConfig.ringCount = value
                                        }
                                        Label {
                                            text: qsTr("Ring spacing:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 1; to: 100
                                            value: Math.round(OwnShipConfig.ringSpacing)
                                            onValueModified: OwnShipConfig.ringSpacing = value
                                        }
                                        Label {
                                            text: qsTr("Ring unit:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Nautical miles"), qsTr("Kilometres"),
                                                    qsTr("Statute miles")]
                                            currentIndex: OwnShipConfig.ringUnit
                                            onActivated: OwnShipConfig.ringUnit = currentIndex
                                        }
                                    }
                                    Label {
                                        text: qsTr("Real-scale icon, GPS offsets and range rings are saved; the own-ship marker is a fixed symbol until those render paths land.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 11
                                    }
                                }
                            }

                            // --- AIS Targets: CPA/TCPA, lost, display, alerts. ---
                            ScrollView {
                                id: aisScroll
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                contentWidth: availableWidth
                                ColumnLayout {
                                    width: aisScroll.availableWidth
                                    spacing: 8

                                    Label { text: qsTr("CPA / TCPA"); font.bold: true }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8; rowSpacing: 8
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("Max target range (NM):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        TextField {
                                            implicitWidth: 90
                                            text: AisConfig.cpaMaxRangeNm.toFixed(1)
                                            validator: DoubleValidator { bottom: 0; top: 100; decimals: 1 }
                                            selectByMouse: true
                                            onEditingFinished: AisConfig.cpaMaxRangeNm = parseFloat(text)
                                        }
                                        Label {
                                            text: qsTr("CPA warning (NM):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        TextField {
                                            implicitWidth: 90
                                            text: AisConfig.cpaWarnNm.toFixed(2)
                                            validator: DoubleValidator { bottom: 0; top: 50; decimals: 2 }
                                            selectByMouse: true
                                            onEditingFinished: AisConfig.cpaWarnNm = parseFloat(text)
                                        }
                                        Label {
                                            text: qsTr("TCPA warning (min):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 120
                                            value: Math.round(AisConfig.tcpaWarnMin)
                                            onValueModified: AisConfig.tcpaWarnMin = value
                                        }
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Lost targets"); font.bold: true }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8; rowSpacing: 8
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("Mark lost after (min):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 60
                                            value: Math.round(AisConfig.markLostMin)
                                            onValueModified: AisConfig.markLostMin = value
                                        }
                                        Label {
                                            text: qsTr("Remove after (min):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 60
                                            value: Math.round(AisConfig.removeLostMin)
                                            onValueModified: AisConfig.removeLostMin = value
                                        }
                                        Label {
                                            text: qsTr("Keep trail history (days):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 90
                                            value: AisConfig.trackRetentionDays
                                            onValueModified: AisConfig.trackRetentionDays = value
                                            ToolTip.visible: hovered
                                            ToolTip.text: qsTr("Days of AIS position history kept in the database for vessel trails (0 = none).")
                                        }
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Display"); font.bold: true }
                                    CheckBox {
                                        text: qsTr("Show target names")
                                        checked: AisConfig.showNames
                                        onToggled: AisConfig.showNames = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Show targets at real size")
                                        checked: AisConfig.showRealSize
                                        onToggled: AisConfig.showRealSize = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Show area notices")
                                        checked: AisConfig.showAreaNotices
                                        onToggled: AisConfig.showAreaNotices = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Process WPL position messages")
                                        checked: AisConfig.handleWplMessages
                                        onToggled: AisConfig.handleWplMessages = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Sync predictor length with own ship")
                                        checked: AisConfig.syncPredictorWithOwnShip
                                        onToggled: AisConfig.syncPredictorWithOwnShip = checked
                                    }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8; rowSpacing: 8
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("COG predictor (min):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 60
                                            enabled: !AisConfig.syncPredictorWithOwnShip
                                            value: Math.round(AisConfig.predictorMinutes)
                                            onValueModified: AisConfig.predictorMinutes = value
                                        }
                                        Label {
                                            text: qsTr("Target tracks (min):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 120
                                            value: Math.round(AisConfig.tracksLengthMin)
                                            onValueModified: AisConfig.tracksLengthMin = value
                                        }
                                        Label {
                                            text: qsTr("Suppress anchored below (kn):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        TextField {
                                            implicitWidth: 90
                                            text: AisConfig.suppressAnchoredSpeedMax.toFixed(1)
                                            validator: DoubleValidator { bottom: 0; top: 20; decimals: 1 }
                                            selectByMouse: true
                                            onEditingFinished: AisConfig.suppressAnchoredSpeedMax = parseFloat(text)
                                        }
                                        Label {
                                            text: qsTr("Attenuate above (targets):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        SpinBox {
                                            from: 0; to: 5000; stepSize: 50
                                            value: AisConfig.attenuationThreshold
                                            onValueModified: AisConfig.attenuationThreshold = value
                                        }
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Rollover info"); font.bold: true }
                                    CheckBox {
                                        text: qsTr("Class / type / status")
                                        checked: AisConfig.rolloverClass
                                        onToggled: AisConfig.rolloverClass = checked
                                    }
                                    CheckBox {
                                        text: qsTr("SOG / COG")
                                        checked: AisConfig.rolloverCogSog
                                        onToggled: AisConfig.rolloverCogSog = checked
                                    }
                                    CheckBox {
                                        text: qsTr("CPA / TCPA")
                                        checked: AisConfig.rolloverCpaTcpa
                                        onToggled: AisConfig.rolloverCpaTcpa = checked
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Alerts"); font.bold: true }
                                    CheckBox {
                                        text: qsTr("CPA/TCPA alert dialog")
                                        checked: AisConfig.cpaAlert
                                        onToggled: AisConfig.cpaAlert = checked
                                    }
                                    RowLayout {
                                        CheckBox {
                                            text: qsTr("Alert sound")
                                            enabled: AisConfig.cpaAlert
                                            checked: AisConfig.cpaAlertSound
                                            onToggled: AisConfig.cpaAlertSound = checked
                                        }
                                        Button {
                                            text: qsTr("Test")
                                            enabled: false  // pending the sound engine
                                        }
                                    }
                                    CheckBox {
                                        text: qsTr("Suppress alerts for moored targets")
                                        enabled: AisConfig.cpaAlert
                                        checked: AisConfig.suppressMooredAlerts
                                        onToggled: AisConfig.suppressMooredAlerts = checked
                                    }
                                    RowLayout {
                                        enabled: AisConfig.cpaAlert
                                        Label { text: qsTr("Acknowledge timeout (min):") }
                                        SpinBox {
                                            from: 0; to: 60
                                            value: Math.round(AisConfig.ackTimeoutMin)
                                            onValueModified: AisConfig.ackTimeoutMin = value
                                        }
                                    }
                                    Label {
                                        text: qsTr("These settings are saved now; CPA/TCPA computation, target filtering and the alert engine are not wired in yet.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 11
                                    }
                                }
                            }

                            // --- MMSI Properties: per-MMSI editor (pending). ---
                            Item {
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 8
                                    Label { text: qsTr("MMSI properties"); font.bold: true }
                                    Label {
                                        text: qsTr("Per-MMSI rules — track mode (default / always / never), persist track, ignore, treat as MOB, VDM follower and a custom ship name — map to the model's MmsiProperties / AIS name-file API, which is not yet bound into the Qt build. The per-MMSI list editor will live here.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText
                                    }
                                    Item { Layout.fillHeight: true }
                                }
                            }
                        }
                    }
                }

                // --- User Interface: General Options + Sounds sub-tabs. ---
                Item {
                    // One file picker, retargeted per sound row. onAccepted
                    // writes the chosen path back to the row's UIConfig field.
                    FileDialog {
                        id: soundFileDialog
                        property string target: ""
                        title: qsTr("Choose sound file")
                        nameFilters: [qsTr("Audio files (*.wav *.mp3 *.ogg *.aiff)"),
                                      qsTr("All files (*)")]
                        onAccepted: {
                            var f = selectedFile.toString()
                            if (target === "anchor") UIConfig.anchorSoundFile = f
                            else if (target === "ais") UIConfig.aisSoundFile = f
                            else if (target === "sart") UIConfig.sartSoundFile = f
                            else if (target === "dsc") UIConfig.dscSoundFile = f
                        }
                    }

                    // Colour pickers shared by the Routes & Marks sub-tab.
                    ColorDialog {
                        id: routeColorDialog
                        selectedColor: RouteDefaultsConfig.routeColor
                        onAccepted: RouteDefaultsConfig.routeColor = selectedColor
                    }
                    ColorDialog {
                        id: trackColorDialog
                        selectedColor: RouteDefaultsConfig.trackColor
                        onAccepted: RouteDefaultsConfig.trackColor = selectedColor
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 12

                        TabBar {
                            id: uiSubTabs
                            Layout.fillWidth: true
                            TabButton { text: qsTr("General Options") }
                            TabButton { text: qsTr("Sounds") }
                            TabButton { text: qsTr("Routes & Marks") }
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: uiSubTabs.currentIndex

                            // --- General Options ---
                            ScrollView {
                                id: uiGenScroll
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                contentWidth: availableWidth
                                ColumnLayout {
                                    width: uiGenScroll.availableWidth
                                    spacing: 8

                                    Label { text: qsTr("General"); font.bold: true }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: qsTr("Language:") }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("System default"), "English",
                                                    "Deutsch", "Français", "Español",
                                                    "Nederlands", "Italiano"]
                                            currentIndex: UIConfig.language
                                            onActivated: UIConfig.language = currentIndex
                                        }
                                    }
                                    Label {
                                        text: qsTr("Language and per-element fonts are saved; they take effect once Qt Linguist translations and a font manager are in place.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 11
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Show"); font.bold: true }
                                    CheckBox {
                                        text: qsTr("Status bar")
                                        checked: UIConfig.showStatusBar
                                        onToggled: UIConfig.showStatusBar = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Chart bar")
                                        checked: UIConfig.showChartBar
                                        onToggled: UIConfig.showChartBar = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Compass / GPS window")
                                        checked: DisplayConfig.showCompass
                                        onToggled: DisplayConfig.showCompass = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Zoom buttons")
                                        checked: UIConfig.showZoomButtons
                                        onToggled: UIConfig.showZoomButtons = checked
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Toolbar"); font.bold: true }
                                    CheckBox {
                                        text: qsTr("Auto-hide toolbar")
                                        checked: UIConfig.autoHideToolbar
                                        onToggled: UIConfig.autoHideToolbar = checked
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        enabled: UIConfig.autoHideToolbar
                                        Label { text: qsTr("Hide after (s):") }
                                        SpinBox {
                                            from: 1; to: 60
                                            value: UIConfig.autoHideTimeout
                                            onValueModified: UIConfig.autoHideTimeout = value
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: qsTr("Transparency:") }
                                        Slider {
                                            Layout.fillWidth: true
                                            from: 0.0; to: 0.9; stepSize: 0.05
                                            value: UIConfig.toolbarTransparency
                                            onMoved: UIConfig.toolbarTransparency = value
                                        }
                                        Label {
                                            text: Math.round(UIConfig.toolbarTransparency * 100) + "%"
                                            font.family: "monospace"
                                        }
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Interface"); font.bold: true }
                                    CheckBox {
                                        text: qsTr("Touchscreen interface")
                                        checked: UIConfig.touchInterface
                                        onToggled: UIConfig.touchInterface = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Use Inland ECDIS")
                                        checked: UIConfig.inlandEcdis
                                        onToggled: UIConfig.inlandEcdis = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Play ship's bells")
                                        checked: UIConfig.playShipsBells
                                        onToggled: UIConfig.playShipsBells = checked
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Scale factors"); font.bold: true }
                                    GridLayout {
                                        columns: 3
                                        columnSpacing: 8; rowSpacing: 6
                                        Layout.fillWidth: true

                                        Label { text: qsTr("User interface:"); Layout.alignment: Qt.AlignRight }
                                        Slider {
                                            Layout.fillWidth: true
                                            from: -5; to: 5; stepSize: 1; snapMode: Slider.SnapAlways
                                            value: UIConfig.guiScaleFactor
                                            onMoved: UIConfig.guiScaleFactor = value
                                        }
                                        Label { text: UIConfig.guiScaleFactor.toString(); font.family: "monospace" }

                                        Label { text: qsTr("Chart objects:"); Layout.alignment: Qt.AlignRight }
                                        Slider {
                                            Layout.fillWidth: true
                                            from: -5; to: 5; stepSize: 1; snapMode: Slider.SnapAlways
                                            value: UIConfig.chartObjectScaleFactor
                                            onMoved: UIConfig.chartObjectScaleFactor = value
                                        }
                                        Label { text: UIConfig.chartObjectScaleFactor.toString(); font.family: "monospace" }

                                        Label { text: qsTr("Ship:"); Layout.alignment: Qt.AlignRight }
                                        Slider {
                                            Layout.fillWidth: true
                                            from: -5; to: 5; stepSize: 1; snapMode: Slider.SnapAlways
                                            value: UIConfig.shipScaleFactor
                                            onMoved: UIConfig.shipScaleFactor = value
                                        }
                                        Label { text: UIConfig.shipScaleFactor.toString(); font.family: "monospace" }

                                        Label { text: qsTr("ENC text:"); Layout.alignment: Qt.AlignRight }
                                        Slider {
                                            Layout.fillWidth: true
                                            from: -5; to: 5; stepSize: 1; snapMode: Slider.SnapAlways
                                            value: UIConfig.encTextScaleFactor
                                            onMoved: UIConfig.encTextScaleFactor = value
                                        }
                                        Label { text: UIConfig.encTextScaleFactor.toString(); font.family: "monospace" }

                                        Label { text: qsTr("ENC soundings:"); Layout.alignment: Qt.AlignRight }
                                        Slider {
                                            Layout.fillWidth: true
                                            from: -5; to: 5; stepSize: 1; snapMode: Slider.SnapAlways
                                            value: UIConfig.encSoundingScaleFactor
                                            onMoved: UIConfig.encSoundingScaleFactor = value
                                        }
                                        Label { text: UIConfig.encSoundingScaleFactor.toString(); font.family: "monospace" }
                                    }
                                    Label {
                                        text: qsTr("The interface scale factor resizes the toolbar/controls live. Chart-object, ship and ENC scale factors are saved and apply once the renderer honours them.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 11
                                    }
                                }
                            }

                            // --- Sounds ---
                            ScrollView {
                                id: uiSoundScroll
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                contentWidth: availableWidth
                                ColumnLayout {
                                    width: uiSoundScroll.availableWidth
                                    spacing: 10

                                    // Per-event sound row: enable + path + browse + test.
                                    component SoundRow: ColumnLayout {
                                        id: soundRow
                                        Layout.fillWidth: true
                                        spacing: 2
                                        property string label
                                        property string key
                                        property bool soundEnabled
                                        property string file
                                        RowLayout {
                                            Layout.fillWidth: true
                                            CheckBox {
                                                text: soundRow.label
                                                checked: soundRow.soundEnabled
                                                onToggled: {
                                                    if (soundRow.key === "anchor") UIConfig.anchorAlarmSound = checked
                                                    else if (soundRow.key === "ais") UIConfig.aisAlertSound = checked
                                                    else if (soundRow.key === "sart") UIConfig.sartAlertSound = checked
                                                    else if (soundRow.key === "dsc") UIConfig.dscAlertSound = checked
                                                }
                                            }
                                            Item { Layout.fillWidth: true }
                                            Button {
                                                text: qsTr("Browse…")
                                                onClicked: { soundFileDialog.target = soundRow.key; soundFileDialog.open() }
                                            }
                                            Button {
                                                text: qsTr("Test")
                                                enabled: soundRow.file.length > 0
                                                onClicked: SoundPlayer.play(soundRow.file)
                                            }
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: soundRow.file.length > 0 ? soundRow.file : qsTr("(no file chosen)")
                                            elide: Text.ElideMiddle
                                            color: palette.placeholderText; font.pointSize: 11
                                        }
                                    }

                                    Label { text: qsTr("Alert sounds"); font.bold: true }
                                    SoundRow { label: qsTr("Anchor alarm"); key: "anchor"
                                        soundEnabled: UIConfig.anchorAlarmSound; file: UIConfig.anchorSoundFile }
                                    SoundRow { label: qsTr("AIS alert"); key: "ais"
                                        soundEnabled: UIConfig.aisAlertSound; file: UIConfig.aisSoundFile }
                                    SoundRow { label: qsTr("AIS SART"); key: "sart"
                                        soundEnabled: UIConfig.sartAlertSound; file: UIConfig.sartSoundFile }
                                    SoundRow { label: qsTr("DSC"); key: "dsc"
                                        soundEnabled: UIConfig.dscAlertSound; file: UIConfig.dscSoundFile }

                                    Label {
                                        text: qsTr("Test plays the chosen file through the Qt sound engine. Automatic triggering of each alert (anchor watch, AIS CPA, SART, DSC) is wired as the alert engine lands.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 11
                                    }
                                }
                            }
                            // --- Routes / Points defaults. ---
                            ScrollView {
                                id: routesScroll
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                contentWidth: availableWidth
                                ColumnLayout {
                                    width: routesScroll.availableWidth
                                    spacing: 8

                                    Label { text: qsTr("New route"); font.bold: true }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8; rowSpacing: 8
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("Line colour:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        Rectangle {
                                            Layout.preferredWidth: 60
                                            Layout.preferredHeight: 24
                                            radius: 4
                                            color: RouteDefaultsConfig.routeColor
                                            border.color: "#80808080"
                                            TapHandler { onTapped: routeColorDialog.open() }
                                        }
                                        Label {
                                            text: qsTr("Line style:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Solid"), qsTr("Dot"), qsTr("Long dash"),
                                                    qsTr("Short dash"), qsTr("Dash-dot")]
                                            currentIndex: RouteDefaultsConfig.routeStyle
                                            onActivated: RouteDefaultsConfig.routeStyle = currentIndex
                                        }
                                    }
                                    CheckBox {
                                        text: qsTr("Persist active route across restarts")
                                        checked: RouteDefaultsConfig.persistActiveRoute
                                        onToggled: RouteDefaultsConfig.persistActiveRoute = checked
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Waypoints"); font.bold: true }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8; rowSpacing: 8
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("Default mark icon:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: RouteDefaultsConfig.waypointIcon
                                            selectByMouse: true
                                            onEditingFinished: RouteDefaultsConfig.waypointIcon = text
                                        }
                                        Label {
                                            text: qsTr("Arrival circle (NM):")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        TextField {
                                            implicitWidth: 90
                                            text: RouteDefaultsConfig.arrivalCircleNm.toFixed(2)
                                            validator: DoubleValidator { bottom: 0; top: 10; decimals: 2 }
                                            selectByMouse: true
                                            onEditingFinished: RouteDefaultsConfig.arrivalCircleNm = parseFloat(text)
                                        }
                                        Label {
                                            text: qsTr("SCAMIN min / max:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        RowLayout {
                                            SpinBox {
                                                from: 0; to: 4000000; stepSize: 1000
                                                value: RouteDefaultsConfig.scaminMin
                                                onValueModified: RouteDefaultsConfig.scaminMin = value
                                            }
                                            SpinBox {
                                                from: 0; to: 4000000; stepSize: 1000
                                                value: RouteDefaultsConfig.scaminMax
                                                onValueModified: RouteDefaultsConfig.scaminMax = value
                                            }
                                        }
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Tracks"); font.bold: true }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8; rowSpacing: 8
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("Auto-create daily:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Off"), qsTr("Computer time"),
                                                    qsTr("UTC"), qsTr("Local mean time")]
                                            currentIndex: RouteDefaultsConfig.trackAutoDaily
                                            onActivated: RouteDefaultsConfig.trackAutoDaily = currentIndex
                                        }
                                        Label {
                                            text: qsTr("Precision:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("High"), qsTr("Medium"), qsTr("Low")]
                                            currentIndex: RouteDefaultsConfig.trackingPrecision
                                            onActivated: RouteDefaultsConfig.trackingPrecision = currentIndex
                                        }
                                        Label {
                                            text: qsTr("Highlight colour:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        RowLayout {
                                            CheckBox {
                                                text: qsTr("Highlight")
                                                checked: RouteDefaultsConfig.trackHighlight
                                                onToggled: RouteDefaultsConfig.trackHighlight = checked
                                            }
                                            Rectangle {
                                                Layout.preferredWidth: 60
                                                Layout.preferredHeight: 24
                                                radius: 4
                                                opacity: RouteDefaultsConfig.trackHighlight ? 1.0 : 0.4
                                                color: RouteDefaultsConfig.trackColor
                                                border.color: "#80808080"
                                                TapHandler {
                                                    enabled: RouteDefaultsConfig.trackHighlight
                                                    onTapped: trackColorDialog.open()
                                                }
                                            }
                                        }
                                    }
                                    Label {
                                        text: qsTr("Saved as defaults; new routes, marks and tracks will adopt them as the creation paths gain styling.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 11
                                    }
                                }
                            }
                        }
                    }
                }

                // --- Plugins (placeholder) ---
                Item {
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 8
                        Label { text: qsTr("Plugins"); font.bold: true }
                        Label {
                            text: qsTr("Plugin management is not yet available in the Qt build.")
                            wrapMode: Text.Wrap; Layout.fillWidth: true
                            color: palette.placeholderText
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }
            }
        }
    }

    // --- About: mirrors the wx About dialog's content, branded OpenCPN-NG. --
    Window {
        id: aboutWindow
        title: qsTr("About OpenCPN-NG")
        flags: Qt.Dialog
        width: 480
        height: 380
        color: palette.window

        // Hyperlink-style label (mirrors the wx About's hyperlinks).
        component Link: Label {
            property string url
            color: "#3478f6"
            font.underline: true
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: Qt.openUrlExternally(parent.url) }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 6

            Label {
                text: "OpenCPN-NG"
                font.pointSize: 24; font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: qsTr("The Open Source Chart Plotter")
                opacity: 0.8
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: qsTr("Version %1  ·  Qt edition").arg(appVersion)
                opacity: 0.9
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: "© 2000–2026 David S. Register and the OpenCPN Authors"
                opacity: 0.7; font.pointSize: 10
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: qsTr("OpenCPN is a Free Software project, built by sailors.\n" +
                           "This edition renders S-57 / S-52 vector charts through a " +
                           "Qt Quick scene graph.")
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                Layout.topMargin: 6
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
                spacing: 20
                Link { text: qsTr("Website"); url: "https://opencpn.org" }
                Link { text: qsTr("GitHub"); url: "https://github.com/OpenCPN/OpenCPN" }
                Link { text: qsTr("Donate")
                       url: "https://sourceforge.net/donate/index.php?group_id=180842" }
                Link { text: qsTr("License")
                       url: "https://www.gnu.org/licenses/old-licenses/gpl-2.0.html" }
            }
            Label {
                text: qsTr("Running on Qt ") + qtRuntimeVersion
                opacity: 0.6; font.pointSize: 9
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 6
            }
            Item { Layout.fillHeight: true }
            DialogButtonBox {
                Layout.fillWidth: true
                standardButtons: DialogButtonBox.Close
                onRejected: aboutWindow.close()
            }
        }
    }

    // --- Data Monitor: scrolling view of decoded NMEA/N2K messages, to
    //     diagnose what a connection is actually delivering.
    Window {
        id: dataMonitorWindow
        title: qsTr("Data monitor")
        flags: Qt.Dialog
        width: 580
        height: 420
        color: palette.window

        // Source filter ("" = all). Lets you isolate one connection to see if
        // it is delivering -- e.g. select the N2000 source to check it.
        property string srcFilter: ""

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                CheckBox {
                    text: qsTr("Pause")
                    checked: chart.nmeaMonitor.paused
                    onToggled: chart.nmeaMonitor.paused = checked
                }
                Label { text: qsTr("Source:") }
                ComboBox {
                    id: srcCombo
                    Layout.preferredWidth: 240
                    model: [qsTr("All")].concat(chart.nmeaMonitor.sources)
                    onActivated: dataMonitorWindow.srcFilter =
                                 (currentIndex === 0 ? "" : currentText)
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: monModel.count + qsTr(" lines")
                    color: palette.placeholderText
                }
                Button {
                    text: qsTr("Clear")
                    onClicked: monModel.clear()
                }
            }
            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 2
                ListView {
                    id: monView
                    anchors.fill: parent
                    clip: true
                    model: ListModel { id: monModel }
                    delegate: Text {
                        required property string line
                        required property string src
                        readonly property bool shown:
                            dataMonitorWindow.srcFilter === "" ||
                            dataMonitorWindow.srcFilter === src
                        width: monView.width
                        height: shown ? implicitHeight : 0
                        visible: shown
                        text: line
                        font.family: "monospace"
                        font.pointSize: 11
                        elide: Text.ElideRight
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
            }
        }

        Connections {
            target: chart.nmeaMonitor
            function onLineReceived(line, source) {
                monModel.append({ "line": line, "src": source })
                if (monModel.count > 1000) monModel.remove(0)
                monView.positionViewAtEnd()  // auto-scroll
            }
        }
    }

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
