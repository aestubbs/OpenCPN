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
    title: qsTr("OpenCPN (Qt prototype)")

    // Minimum touch target (logical px) for the on-chart controls. Scaled by
    // the UI scale factor (Options > User Interface): -5..+5 -> ~0.4x..~1.6x.
    readonly property int touchSize: Math.round(40 * (1 + 0.12 * UIConfig.guiScaleFactor))

    // Toggle for the on-chart debug/stats overlay (like an FPS counter).
    property bool showDebug: false

    // Expandable vessel-data HUD panel on the right edge (own-ship gauges).
    property bool hudExpanded: false

    // wx "Hide Toolbar": collapse the floating master toolbar to its toggle.
    property bool toolbarCollapsed: false

    // Native window status bar: cursor lat/lon (left) + chart scale (right).
    footer: ToolBar {
        visible: UIConfig.showStatusBar  // Options > User Interface
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

            Label { text: qsTr("Detail"); font.bold: true }
            CheckBox {
                text: qsTr("Soundings")
                checked: chart.showSoundings
                onToggled: chart.showSoundings = checked
            }
            CheckBox {
                text: qsTr("Text labels")
                checked: chart.showText
                onToggled: chart.showText = checked
            }
            CheckBox {
                text: qsTr("Lights")
                checked: chart.showLights
                onToggled: chart.showLights = checked
            }
            CheckBox {
                text: qsTr("Buoys & beacons")
                checked: chart.showBuoys
                onToggled: chart.showBuoys = checked
            }

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
            }
            Item { Layout.fillHeight: true }
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
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: chartsSubTabs.currentIndex

                            // --- Chart Files (pending runtime chart-dir backend) ---
                            Item {
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 8
                                    Label { text: qsTr("Chart files"); font.bold: true }
                                    Label {
                                        text: qsTr("The Qt build loads its chart set from a path fixed at build time, so the chart-directory list, database scan/rebuild and ENC pre-processing controls are not yet available. They arrive with the runtime chart-directory manager.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText
                                    }
                                    Item { Layout.fillHeight: true }
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

                                    Label { text: qsTr("Detail (live)"); font.bold: true }
                                    CheckBox {
                                        text: qsTr("Soundings")
                                        checked: chart.showSoundings
                                        onToggled: chart.showSoundings = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Text labels")
                                        checked: chart.showText
                                        onToggled: chart.showText = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Lights")
                                        checked: chart.showLights
                                        onToggled: chart.showLights = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Buoys & beacons")
                                        checked: chart.showBuoys
                                        onToggled: chart.showBuoys = checked
                                    }

                                    MenuSeparator { Layout.fillWidth: true }

                                    Label { text: qsTr("Cartography & text"); font.bold: true }
                                    CheckBox {
                                        text: qsTr("Chart information objects")
                                        checked: ChartConfig.chartInfoObjects
                                        onToggled: ChartConfig.chartInfoObjects = checked
                                    }
                                    CheckBox {
                                        text: qsTr("Buoy & light labels")
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
                                        text: qsTr("Super SCAMIN")
                                        checked: ChartConfig.superScamin
                                        onToggled: ChartConfig.superScamin = checked
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

                                    Label { text: qsTr("Depth contours (m)"); font.bold: true }
                                    GridLayout {
                                        columns: 2
                                        columnSpacing: 8; rowSpacing: 8
                                        Layout.fillWidth: true
                                        Label { text: qsTr("Shallow:"); Layout.alignment: Qt.AlignRight }
                                        SpinBox {
                                            from: 0; to: 50
                                            value: Math.round(ChartConfig.shallowContour)
                                            onValueModified: ChartConfig.shallowContour = value
                                        }
                                        Label { text: qsTr("Safety:"); Layout.alignment: Qt.AlignRight }
                                        SpinBox {
                                            from: 0; to: 50
                                            value: Math.round(ChartConfig.safetyContour)
                                            onValueModified: ChartConfig.safetyContour = value
                                        }
                                        Label { text: qsTr("Deep:"); Layout.alignment: Qt.AlignRight }
                                        SpinBox {
                                            from: 0; to: 100
                                            value: Math.round(ChartConfig.deepContour)
                                            onValueModified: ChartConfig.deepContour = value
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

                            // --- Chart Groups (pending) ---
                            Item {
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 8
                                    Label { text: qsTr("Chart groups"); font.bold: true }
                                    Label {
                                        text: qsTr("Named chart groups depend on the chart-directory manager and are not yet available in the Qt build.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText
                                    }
                                    Item { Layout.fillHeight: true }
                                }
                            }

                            // --- Tides & Currents (pending) ---
                            Item {
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 8
                                    Label { text: qsTr("Tides & currents"); font.bold: true }
                                    Label {
                                        text: qsTr("Tide and current harmonic data sets are not yet loaded by the Qt build; the data-location list will live here.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText
                                    }
                                    Item { Layout.fillHeight: true }
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

                        Label { text: qsTr("Network data sources"); font.bold: true }

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
                                        text: "✕"
                                        onClicked: chart.connections.removeConnection(index)
                                    }
                                }
                            }
                        }

                        MenuSeparator { Layout.fillWidth: true }

                        Label { text: qsTr("Add connection"); font.bold: true }
                        GridLayout {
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8
                            Layout.fillWidth: true

                            Label {
                                text: qsTr("Transport:")
                                Layout.alignment: Qt.AlignRight
                            }
                            ComboBox {
                                id: netProtoBox
                                Layout.fillWidth: true
                                model: ["TCP", "UDP"]
                            }
                            Label {
                                text: qsTr("Data protocol:")
                                Layout.alignment: Qt.AlignRight
                            }
                            ComboBox {
                                id: dataProtoBox
                                Layout.fillWidth: true
                                model: ["NMEA 0183", "NMEA 2000", "SignalK"]
                            }
                            Label {
                                text: qsTr("Address / host:")
                                Layout.alignment: Qt.AlignRight
                            }
                            TextField {
                                id: addrField
                                Layout.fillWidth: true
                                placeholderText: qsTr("e.g. 0.0.0.0 or 192.168.1.10")
                                selectByMouse: true
                            }
                            Label {
                                text: qsTr("Port:")
                                Layout.alignment: Qt.AlignRight
                            }
                            TextField {
                                id: portField
                                Layout.fillWidth: true
                                placeholderText: qsTr("e.g. 2000 / 60001")
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: IntValidator { bottom: 1; top: 65535 }
                                selectByMouse: true
                            }
                            Item {}  // spacer in label column
                            Button {
                                text: qsTr("Add")
                                Layout.alignment: Qt.AlignLeft
                                enabled: addrField.text.length > 0 && portField.text.length > 0
                                onClicked: {
                                    chart.connections.addConnection(
                                        netProtoBox.currentIndex, addrField.text,
                                        parseInt(portField.text), dataProtoBox.currentIndex)
                                    addrField.text = ""; portField.text = ""
                                }
                            }
                        }
                        Label {
                            text: qsTr("Enabling a connection opens the socket and switches to live data.")
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
                            TabButton { text: qsTr("Routes/Points") }
                        }

                        // Colour pickers shared by the Routes/Points sub-tab.
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
                                            text: qsTr("Default route-point icon:")
                                            Layout.alignment: Qt.AlignRight
                                        }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: RouteDefaultsConfig.routepointIcon
                                            selectByMouse: true
                                            onEditingFinished: RouteDefaultsConfig.routepointIcon = text
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

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 12

                        TabBar {
                            id: uiSubTabs
                            Layout.fillWidth: true
                            TabButton { text: qsTr("General Options") }
                            TabButton { text: qsTr("Sounds") }
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
                                            Button { text: qsTr("Test"); enabled: false }
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
                                        text: qsTr("Sound playback is not yet wired in; these choices are saved for when the Qt sound engine lands. Test is disabled until then.")
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
                text: qsTr("A chart plotter and marine GPS navigation DisplayConfig.\n" +
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
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        // Right edge follows the HUD panel so opening it shrinks the chart.
        anchors.right: hudPanel.left
        // Hand the S-52 engine to the canvas so it scans the chart set's
        // boundaries and streams cell content on demand. `s52` is the
        // context property set in main.cpp.
        s52Engine: s52

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

        // --- MUIBar: per-canvas controls bottom-right, mirroring OpenCPN's
        //     MUIBar -- zoom in/out, follow own ship, and a menu opening the
        //     canvas display options. (Distinct from the master toolbar.)
        Pane {
            id: muiBar
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 12
            padding: 3
            RowLayout {
                spacing: 2
                component MuiTool: ToolButton {
                    font.pointSize: 16
                    implicitWidth: 36; implicitHeight: 32
                    ToolTip.visible: hovered && ToolTip.text.length > 0
                    ToolTip.delay: 400
                }
                MuiTool {
                    text: "+"; font.pointSize: 19; ToolTip.text: qsTr("Zoom in")
                    visible: UIConfig.showZoomButtons  // Options > User Interface
                    onClicked: chart.zoomIn()
                }
                MuiTool {
                    text: "−"; font.pointSize: 19; ToolTip.text: qsTr("Zoom out")
                    visible: UIConfig.showZoomButtons
                    onClicked: chart.zoomOut()
                }
                MuiTool {
                    text: "⤢"; font.pointSize: 14; ToolTip.text: qsTr("Fit / zoom to world")
                    onClicked: chart.fitWorld()
                }
                MuiTool {
                    text: "⊙"; ToolTip.text: qsTr("Auto-follow own ship")
                    checkable: true
                    checked: chart.followOwnShip
                    onClicked: chart.followOwnShip = checked
                }
                MuiTool {
                    text: "☰"; ToolTip.text: qsTr("Canvas display options")
                    onClicked: canvasOptions.open()
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
        // Toolbar transparency (Options > User Interface).
        opacity: 1.0 - UIConfig.toolbarTransparency

        // Auto-hide (Options > User Interface): collapse to the toggle after a
        // period of no hover; pointing at it expands it again.
        HoverHandler {
            id: tbHover
            onHoveredChanged: if (hovered && UIConfig.autoHideToolbar)
                                  root.toolbarCollapsed = false
        }
        Timer {
            interval: Math.max(1, UIConfig.autoHideTimeout) * 1000
            running: UIConfig.autoHideToolbar && !tbHover.hovered
            onTriggered: root.toolbarCollapsed = true
        }

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

            // wx ID_MASTERTOGGLE: collapse/expand the master toolbar. The
            // rest of the tools hide when collapsed, leaving just this button.
            Tool {
                text: "☰"
                ToolTip.text: root.toolbarCollapsed ? qsTr("Show toolbar")
                                                    : qsTr("Hide toolbar")
                onClicked: root.toolbarCollapsed = !root.toolbarCollapsed
            }
            // wx ID_SETTINGS.
            Tool {
                visible: !root.toolbarCollapsed
                text: "⚙"; ToolTip.text: qsTr("Options")
                onClicked: { optionsWindow.show(); optionsWindow.raise() }
            }
            // wx ID_MENU_ROUTE_NEW.
            Tool {
                visible: !root.toolbarCollapsed
                text: "✚"; checkable: true
                checked: chart.routeBuildMode
                ToolTip.text: qsTr("Create route  (left-click adds points, right-click finishes)")
                onClicked: chart.routeBuildMode = checked
            }
            // wx ID_ROUTEMANAGER.
            Tool {
                visible: !root.toolbarCollapsed
                text: "▤"; ToolTip.text: qsTr("Route && mark manager")
                onClicked: { routeManagerWindow.show(); routeManagerWindow.raise() }
            }
            // wx ID_TRACK.
            Tool {
                visible: !root.toolbarCollapsed
                text: "⊚"; checkable: true
                checked: chart.trackRecording
                ToolTip.text: qsTr("Record own-ship track")
                onClicked: chart.trackRecording = checked
            }
            // wx ID_COLSCHEME: cycle day/dusk/night.
            Tool {
                visible: !root.toolbarCollapsed
                text: "◑"
                ToolTip.text: [qsTr("Color scheme: Day"),
                               qsTr("Color scheme: Dusk"),
                               qsTr("Color scheme: Night")][chart.colorScheme]
                onClicked: chart.colorScheme = (chart.colorScheme + 1) % 3
            }
            // wx ID_PRINT -- not wired yet.
            Tool {
                visible: !root.toolbarCollapsed
                text: "⎙"; ToolTip.text: qsTr("Print chart (not yet implemented)")
            }
            // Data Monitor (Qt addition).
            Tool {
                visible: !root.toolbarCollapsed
                text: "≣"; ToolTip.text: qsTr("Data monitor")
                onClicked: { dataMonitorWindow.show(); dataMonitorWindow.raise() }
            }
            // wx ID_ABOUT.
            Tool {
                visible: !root.toolbarCollapsed
                text: "ⓘ"; ToolTip.text: qsTr("About OpenCPN")
                onClicked: { aboutWindow.show(); aboutWindow.raise() }
            }
            // wx ID_MOB -- not wired yet.
            Tool {
                visible: !root.toolbarCollapsed
                text: "⚓"; ToolTip.text: qsTr("Drop MOB marker (not yet implemented)")
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
