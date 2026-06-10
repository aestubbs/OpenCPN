/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import opencpn.qt

// Options: the tabbed settings window, following the macOS settings HIG:
// a centred preference-style tab toolbar; standard (not touch) native
// controls -- checkboxes/radio buttons, no enlarged sizes; 20pt margins;
// modeless, close via the window control; fixed size, title shows the
// current pane. Extracted from Main.qml (P3.17); `chart` and the config
// singletons resolve via the context chain (same pattern as FloatToolbar).
Window {
    id: optionsWindow

    // The application shell window -- for cross-cutting shell state that
    // lives on the ApplicationWindow (the on-chart debug overlay toggle).
    required property var appWindow

    // Open the Data Monitor window (a sibling of this window, wired in
    // Main.qml). It is a connections-debugging tool, so its launcher lives
    // on the Connections page (P3.22).
    signal dataMonitorRequested()

    // Deep-link entry (wx SetInitialPage parity): show the window opened
    // at a given page index.
    function openAt(page) {
        currentPage = page
        show()
        raise()
    }

    // Persist the window position across sessions (size is fixed by
    // design, like macOS settings windows).
    onXChanged: geometrySaver.restart()
    onYChanged: geometrySaver.restart()
    Timer {
        id: geometrySaver
        interval: 600
        onTriggered: if (optionsWindow.visible) {
            UIConfig.optionsX = optionsWindow.x
            UIConfig.optionsY = optionsWindow.y
        }
    }
    onVisibleChanged: if (visible && UIConfig.optionsX >= 0) {
        x = UIConfig.optionsX
        y = UIConfig.optionsY
    }

    PrioritiesDialog { id: prioritiesDialog }

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
                                    text: qsTr("Show grid (lat/lon graticule)")
                                    checked: DisplayConfig.showGrid
                                    onToggled: DisplayConfig.showGrid = checked
                                }
                                CheckBox {
                                    text: qsTr("Show depth units legend")
                                    checked: DisplayConfig.showDepthUnits
                                    onToggled: DisplayConfig.showDepthUnits = checked
                                }
                                CheckBox {
                                    text: qsTr("Show chart outlines (ENC cell grid)")
                                    checked: DisplayConfig.showChartOutlines
                                    onToggled: DisplayConfig.showChartOutlines = checked
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
                                    checked: appWindow.showDebug
                                    onToggled: appWindow.showDebug = checked
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
                                             { label: qsTr("All"), cat: 2 },
                                             { label: qsTr("Mariner's standard"), cat: 3 } ]
                                    delegate: RadioButton {
                                        required property var modelData
                                        text: modelData.label
                                        ButtonGroup.group: optCatGroup
                                        checked: chart.displayCategory === modelData.cat
                                        onClicked: chart.displayCategory = modelData.cat
                                    }
                                }

                                // "User Standard Objects" (P3.6, wx
                                // MARINERS_STANDARD): per-class visibility,
                                // applied only in Mariner's standard. Checked
                                // = shown; the hidden set persists.
                                ColumnLayout {
                                    id: objFilterPanel
                                    visible: chart.displayCategory === 3
                                    Layout.fillWidth: true
                                    spacing: 6
                                    readonly property var catalog: chart.s57ClassCatalog()
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("User standard objects")
                                            font.bold: true
                                            Layout.fillWidth: true
                                        }
                                        Button {
                                            text: qsTr("Show all")
                                            onClicked: chart.hiddenObjectClasses = []
                                        }
                                        Button {
                                            text: qsTr("Hide all")
                                            onClicked: chart.hiddenObjectClasses =
                                                objFilterPanel.catalog.map(c => c.acronym)
                                        }
                                    }
                                    Frame {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 240
                                        padding: 2
                                        ListView {
                                            id: objFilterList
                                            anchors.fill: parent
                                            clip: true
                                            model: objFilterPanel.catalog
                                            delegate: CheckBox {
                                                required property var modelData
                                                width: objFilterList.width
                                                text: modelData.acronym + " — " +
                                                      modelData.description
                                                font.pointSize: 11
                                                checked: chart.hiddenObjectClasses
                                                         .indexOf(modelData.acronym) < 0
                                                onToggled: {
                                                    var h = chart.hiddenObjectClasses.slice()
                                                    const i = h.indexOf(modelData.acronym)
                                                    if (checked && i >= 0) h.splice(i, 1)
                                                    else if (!checked && i < 0)
                                                        h.push(modelData.acronym)
                                                    chart.hiddenObjectClasses = h
                                                }
                                            }
                                            ScrollBar.vertical: ScrollBar {}
                                        }
                                    }
                                    Label {
                                        text: qsTr("Unchecked classes are hidden in Mariner's standard. Display-base objects (land, coastline, safety contour) always show.")
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                        color: palette.placeholderText; font.pointSize: 10
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

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Data connections"); font.bold: true }
                        Item { Layout.fillWidth: true }
                        Button {
                            // Which source wins per data category when
                            // several connections deliver the same data.
                            text: qsTr("Priorities…")
                            onClicked: prioritiesDialog.show()
                        }
                        Button {
                            // What a connection is actually delivering, live
                            // (decoded NMEA/N2K stream) -- the debug window.
                            text: qsTr("Data monitor…")
                            onClicked: optionsWindow.dataMonitorRequested()
                        }
                    }

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
                                RowLayout {
                                    spacing: 8
                                    Label { text: qsTr("Heading (HDT) predictor length:") }
                                    SpinBox {
                                        from: 0; to: 20
                                        value: Math.round(OwnShipConfig.hdtPredictorNm)
                                        onValueModified: OwnShipConfig.hdtPredictorNm = value
                                    }
                                    Label {
                                        text: qsTr("NM (0 = off; dashed, separate from the COG predictor)")
                                        color: palette.placeholderText; font.pointSize: 11
                                    }
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
                                    Label {
                                        text: qsTr("Ring colour:")
                                        Layout.alignment: Qt.AlignRight
                                    }
                                    Rectangle {
                                        width: 48; height: 22; radius: 4
                                        color: OwnShipConfig.ringColor
                                        border.color: "#80808080"
                                        TapHandler { onTapped: ringColorDialog.open() }
                                    }
                                }
                                ColorDialog {
                                    id: ringColorDialog
                                    selectedColor: OwnShipConfig.ringColor
                                    onAccepted: OwnShipConfig.ringColor = selectedColor
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
                                        // Same file the alert engine fires for
                                        // an AIS CPA danger (UI > Sounds).
                                        onClicked: SoundPlayer.play(UIConfig.aisSoundFile)
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

                            }
                        }

                        // --- MMSI Properties: per-MMSI editor (P3.6, wx
                        //     MmsiProperties). Edits feed the live decoder's
                        //     g_MMSI_Props_Array and persist in the config. ---
                        Item {
                            ColumnLayout {
                                id: mmsiTab
                                anchors.fill: parent
                                anchors.margins: 20
                                spacing: 8

                                property var entries: chart.mmsiProperties()
                                function refresh() { entries = chart.mmsiProperties() }
                                function loadForm(e) {
                                    mmsiField.text = String(e.mmsi)
                                    mmsiNameField.text = e.shipName
                                    mmsiTrackBox.currentIndex = e.trackType
                                    mmsiIgnore.checked = e.ignore
                                    mmsiMob.checked = e.mob
                                    mmsiVdm.checked = e.vdm
                                    mmsiFollower.checked = e.follower
                                    mmsiPersist.checked = e.persistTrack
                                }
                                function clearForm() {
                                    mmsiField.text = ""
                                    mmsiNameField.text = ""
                                    mmsiTrackBox.currentIndex = 0
                                    mmsiIgnore.checked = false
                                    mmsiMob.checked = false
                                    mmsiVdm.checked = false
                                    mmsiFollower.checked = false
                                    mmsiPersist.checked = false
                                }

                                Label { text: qsTr("MMSI properties"); font.bold: true }
                                Label {
                                    text: qsTr("Per-vessel rules applied by the AIS decoder: ignore the target, force its track on or off, persist its track, treat its position reports as your MOB / follower, and a display name.")
                                    wrapMode: Text.Wrap; Layout.fillWidth: true
                                    color: palette.placeholderText; font.pointSize: 11
                                }

                                Frame {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 130
                                    padding: 2
                                    ListView {
                                        id: mmsiList
                                        anchors.fill: parent
                                        clip: true
                                        model: mmsiTab.entries
                                        delegate: ItemDelegate {
                                            required property var modelData
                                            width: mmsiList.width
                                            height: 28
                                            highlighted: mmsiField.text === String(modelData.mmsi)
                                            onClicked: mmsiTab.loadForm(modelData)
                                            contentItem: Label {
                                                text: modelData.mmsi +
                                                      (modelData.shipName.length ? "  ·  " + modelData.shipName : "") +
                                                      (modelData.ignore ? qsTr("  ·  ignored") : "") +
                                                      (modelData.trackType === 1 ? qsTr("  ·  always track")
                                                       : modelData.trackType === 2 ? qsTr("  ·  never track") : "")
                                                font.pointSize: 11
                                                elide: Text.ElideRight
                                            }
                                        }
                                        ScrollBar.vertical: ScrollBar {}
                                    }
                                }

                                GridLayout {
                                    columns: 4
                                    columnSpacing: 8; rowSpacing: 6
                                    Layout.fillWidth: true
                                    Label { text: qsTr("MMSI:"); Layout.alignment: Qt.AlignRight }
                                    TextField {
                                        id: mmsiField
                                        Layout.preferredWidth: 110
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        validator: IntValidator { bottom: 1; top: 999999999 }
                                        selectByMouse: true
                                    }
                                    Label { text: qsTr("Name:"); Layout.alignment: Qt.AlignRight }
                                    TextField {
                                        id: mmsiNameField
                                        Layout.fillWidth: true
                                        selectByMouse: true
                                    }
                                    Label { text: qsTr("Track:"); Layout.alignment: Qt.AlignRight }
                                    ComboBox {
                                        id: mmsiTrackBox
                                        Layout.preferredWidth: 140
                                        model: [qsTr("Default"), qsTr("Always"), qsTr("Never")]
                                    }
                                    CheckBox { id: mmsiPersist; text: qsTr("Persist track") }
                                    CheckBox { id: mmsiIgnore; text: qsTr("Ignore this target") }
                                }
                                RowLayout {
                                    spacing: 8
                                    CheckBox { id: mmsiMob; text: qsTr("Handle as MOB beacon") }
                                    CheckBox { id: mmsiVdm; text: qsTr("Convert VDM to VDO") }
                                    CheckBox { id: mmsiFollower; text: qsTr("Follower vessel") }
                                }
                                RowLayout {
                                    spacing: 8
                                    Button {
                                        text: qsTr("Save")
                                        enabled: parseInt(mmsiField.text) > 0
                                        onClicked: {
                                            chart.saveMmsiProperty({
                                                mmsi: parseInt(mmsiField.text),
                                                shipName: mmsiNameField.text,
                                                trackType: mmsiTrackBox.currentIndex,
                                                ignore: mmsiIgnore.checked,
                                                mob: mmsiMob.checked,
                                                vdm: mmsiVdm.checked,
                                                follower: mmsiFollower.checked,
                                                persistTrack: mmsiPersist.checked
                                            })
                                            mmsiTab.refresh()
                                        }
                                    }
                                    Button {
                                        text: qsTr("New")
                                        onClicked: mmsiTab.clearForm()
                                    }
                                    Button {
                                        text: qsTr("Delete")
                                        enabled: parseInt(mmsiField.text) > 0
                                        onClicked: {
                                            chart.deleteMmsiProperty(parseInt(mmsiField.text))
                                            mmsiTab.clearForm()
                                            mmsiTab.refresh()
                                        }
                                    }
                                    Item { Layout.fillWidth: true }
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
                                    onToggled: {
                                        UIConfig.inlandEcdis = checked
                                        if (checked) {
                                            // wx SwitchInlandEcdisMode preset.
                                            DisplayConfig.distanceUnit = 2  // km
                                            DisplayConfig.speedUnit = 2     // km/h
                                            chart.displayCategory = 1       // Standard
                                            AisConfig.showRealSize = false
                                        }
                                    }
                                }
                                Label {
                                    visible: UIConfig.inlandEcdis
                                    text: qsTr("Units forced to km / km/h, display category Standard, AIS real-size off. Switch off to restore your own settings.")
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                    font.pointSize: 10
                                    color: palette.placeholderText
                                }
                                CheckBox {
                                    text: qsTr("Play ship's bells")
                                    checked: UIConfig.playShipsBells
                                    onToggled: UIConfig.playShipsBells = checked
                                }

                                MenuSeparator { Layout.fillWidth: true }
                                Label { text: qsTr("Configuration templates"); font.bold: true }
                                Label {
                                    text: qsTr("Named snapshots of every setting. Applying a template takes full effect on the next start.")
                                    wrapMode: Text.Wrap; Layout.fillWidth: true
                                    font.pointSize: 10
                                    color: palette.placeholderText
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    TextField {
                                        id: templateName
                                        Layout.fillWidth: true
                                        placeholderText: qsTr("Template name")
                                    }
                                    Button {
                                        text: qsTr("Save current…")
                                        enabled: templateName.text.trim().length > 0
                                        onClicked: {
                                            ConfigTemplates.saveCurrent(templateName.text)
                                            templateName.clear()
                                        }
                                    }
                                }
                                Repeater {
                                    model: ConfigTemplates.templates
                                    delegate: RowLayout {
                                        required property string modelData
                                        Layout.fillWidth: true
                                        Label { text: modelData; Layout.fillWidth: true }
                                        Button {
                                            text: qsTr("Apply")
                                            onClicked: ConfigTemplates.apply(modelData)
                                        }
                                        Button {
                                            text: qsTr("Delete")
                                            onClicked: ConfigTemplates.remove(modelData)
                                        }
                                    }
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

                                // Output device (P3.6): which audio output the
                                // alert sounds play through.
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: qsTr("Output device:") }
                                    ComboBox {
                                        Layout.fillWidth: true
                                        model: SoundPlayer.outputDevices()
                                        currentIndex: SoundPlayer.outputDevice
                                        onActivated: SoundPlayer.outputDevice = currentIndex
                                    }
                                }

                                MenuSeparator { Layout.fillWidth: true }

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
                                CheckBox {
                                    text: qsTr("Drop an anchor mark when the anchor watch is set")
                                    checked: RouteDefaultsConfig.autoAnchorMark
                                    onToggled: RouteDefaultsConfig.autoAnchorMark = checked
                                }
                                CheckBox {
                                    text: qsTr("Confirm before deleting routes, tracks and marks")
                                    checked: RouteDefaultsConfig.confirmObjectDelete
                                    onToggled: RouteDefaultsConfig.confirmObjectDelete = checked
                                }
                                CheckBox {
                                    text: qsTr("Advance the active waypoint only inside the arrival circle")
                                    checked: RouteDefaultsConfig.advanceOnArrivalOnly
                                    onToggled: RouteDefaultsConfig.advanceOnArrivalOnly = checked
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Off (default): passing abeam of the waypoint also advances the route")
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

            // --- Plugins (P4.2): the Qt plugin catalogue. ---
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 8
                    Label { text: qsTr("Plugins"); font.bold: true }
                    Label {
                        text: qsTr("Qt plugin modules found in the application plugins-qt folder. Enabling or disabling applies on restart. (The legacy wx plugins are not loadable here — they port to the Qt API in Phase 4.)")
                        wrapMode: Text.Wrap; Layout.fillWidth: true
                        color: palette.placeholderText; font.pointSize: 11
                    }
                    Frame {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        padding: 2
                        ListView {
                            id: pluginList
                            anchors.fill: parent
                            clip: true
                            model: chart.pluginRegistry.plugins
                            delegate: ItemDelegate {
                                required property var modelData
                                width: pluginList.width
                                contentItem: RowLayout {
                                    spacing: 8
                                    CheckBox {
                                        checked: modelData.enabled
                                        onToggled: chart.pluginRegistry
                                            .setPluginEnabled(modelData.name, checked)
                                    }
                                    ColumnLayout {
                                        spacing: 0
                                        Layout.fillWidth: true
                                        Label {
                                            text: modelData.name +
                                                  (modelData.version ? "  " + modelData.version : "")
                                            font.bold: true
                                        }
                                        Label {
                                            text: modelData.error && modelData.error.length
                                                  ? qsTr("Error: ") + modelData.error
                                                  : (modelData.description || "")
                                            font.pointSize: 10
                                            color: modelData.error && modelData.error.length
                                                   ? "#e05060" : palette.placeholderText
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }
                                    Label {
                                        text: modelData.loaded ? qsTr("loaded") : qsTr("off")
                                        color: palette.placeholderText
                                        font.pointSize: 10
                                    }
                                }
                            }
                            ScrollBar.vertical: ScrollBar {}
                        }
                    }
                    Label {
                        visible: chart.pluginRegistry.plugins.length === 0
                        text: qsTr("No Qt plugins installed.")
                        color: palette.placeholderText
                    }
                    // Plugin-contributed settings pages, stacked below.
                    Repeater {
                        model: chart.pluginRegistry.settingsPages
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Label { text: modelData.title; font.bold: true }
                            Loader {
                                Layout.fillWidth: true
                                source: modelData.component
                                onLoaded: if (item && modelData.context)
                                              item.pluginContext = modelData.context
                            }
                        }
                    }
                }
            }
        }
        }
    }
}
