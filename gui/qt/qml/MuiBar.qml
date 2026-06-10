/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import opencpn.qt

// MUIBar -- per-canvas view controls, bottom-right (mirrors OpenCPN's MUIBar).
// Extracted from Main.qml (P3.17). Zoom in/out, the Follow / jump-to-ship split
// button, and a menu opening the canvas display options. Tides + anchor moved
// to the master toolbar; scale lives in the status bar; the fit-to-world button
// was dropped (it only ever framed the whole globe).
//
// Styling matches FloatToolbar.qml (keep in sync): transparency lives in the
// background colours' alpha (NOT Pane.opacity, which would dim the icons too);
// a translucent panel with a 1px opaque black border; button chips a touch less
// transparent than the panel; icon glyphs fully opaque.
Pane {
    id: muiBar
    padding: 3
    readonly property real panelAlpha: 1.0 - UIConfig.toolbarTransparency
    // Buttons are a touch less transparent than the panel: 40% of the way from
    // the panel's alpha toward opaque (never clamping to fully opaque).
    readonly property real buttonAlpha: panelAlpha + (1.0 - panelAlpha) * 0.4
    // The 1px border is a subtle hairline -- much more transparent than the
    // panel (black reads high-contrast, so keep its alpha well down).
    readonly property real borderAlpha: panelAlpha * 0.4

    signal canvasOptionsRequested()

    background: Rectangle {
        color: Qt.rgba(0.93, 0.93, 0.95, muiBar.panelAlpha)
        border.color: Qt.rgba(0, 0, 0, muiBar.borderAlpha)
        border.width: 1
        radius: 6
    }

    // Shared tool factory (same chip styling as FloatToolbar's Tool): used by
    // the bar itself AND the follow flyout, so they're identical.
    component MuiTool: ToolButton {
        id: ctl
        font.pointSize: 16
        implicitWidth: 36; implicitHeight: 32
        ToolTip.visible: hovered && ToolTip.text.length > 0
        ToolTip.delay: 400
        background: Rectangle {
            radius: 4
            readonly property real a: muiBar.buttonAlpha
            color: (ctl.checked || ctl.down || ctl.highlighted)
                       ? Qt.rgba(0.30, 0.47, 0.75, Math.max(0.9, a))
                   : ctl.hovered ? Qt.rgba(0.78, 0.84, 0.93, a)
                   : Qt.rgba(0.87, 0.88, 0.91, a)
        }
        contentItem: Text {
            text: ctl.text
            font: ctl.font
            color: (ctl.checked || ctl.down || ctl.highlighted) ? "white"
                                                                 : "#1b1d21"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    RowLayout {
        spacing: 2
        MuiTool {
            text: "+"; font.pointSize: 19; ToolTip.text: qsTr("Zoom in")
            visible: UIConfig.showZoomButtons  // Options > User Interface
            onClicked: (root.activeChart || chart).zoomIn()
        }
        MuiTool {
            text: "−"; font.pointSize: 19; ToolTip.text: qsTr("Zoom out")
            visible: UIConfig.showZoomButtons
            onClicked: (root.activeChart || chart).zoomOut()
        }
        // Follow / jump-to-ship split button. Tap = jump to the ship + toggle
        // follow (wx TogglebFollow->SetbFollow->JumpToPosition). Press-and-hold
        // (or right-click) pops a small flyout toolbar of the variants {centred,
        // look-ahead}; choosing one runs it and sticks as the one-click default.
        // The glyph reflects the 3 states (off / follow / follow-ahead);
        // look-ahead = DisplayConfig.lookAhead, honoured by the follow centring.
        MuiTool {
            id: followBtn
            text: !(root.activeChart || chart).followOwnShip ? "⊙"
                  : (DisplayConfig.lookAhead ? "➤" : "◉")
            ToolTip.text: !(root.activeChart || chart).followOwnShip ? qsTr("Follow / jump to ship")
                  : (DisplayConfig.lookAhead ? qsTr("Following (look-ahead)")
                                             : qsTr("Following (centred)"))
            highlighted: (root.activeChart || chart).followOwnShip
            onClicked: { const c = root.activeChart || chart; c.followOwnShip = !c.followOwnShip }
            onPressAndHold: followFlyout.open()
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: followFlyout.open()
            }

            // Flyout toolbar: a mini panel of the same chip-styled buttons that
            // pops up above the follow button. Quick scale+fade animation.
            Popup {
                id: followFlyout
                parent: followBtn
                x: (followBtn.width - width) / 2
                y: -height - 4
                padding: 3
                modal: false
                closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
                background: Rectangle {
                    color: Qt.rgba(0.93, 0.93, 0.95, muiBar.panelAlpha)
                    border.color: Qt.rgba(0, 0, 0, muiBar.borderAlpha); border.width: 1; radius: 6
                }
                enter: Transition {
                    NumberAnimation { property: "opacity"; from: 0.0; to: 1.0
                                      duration: 90 }
                    NumberAnimation { property: "scale"; from: 0.85; to: 1.0
                                      duration: 110; easing.type: Easing.OutBack }
                }
                exit: Transition {
                    NumberAnimation { property: "opacity"; from: 1.0; to: 0.0
                                      duration: 70 }
                }
                ColumnLayout {
                    spacing: 2
                    MuiTool {
                        text: "◉"; ToolTip.text: qsTr("Follow (centred)")
                        highlighted: (root.activeChart || chart).followOwnShip && !DisplayConfig.lookAhead
                        onClicked: { DisplayConfig.lookAhead = false
                                     (root.activeChart || chart).followOwnShip = true
                                     followFlyout.close() }
                    }
                    MuiTool {
                        text: "➤"; ToolTip.text: qsTr("Follow + look-ahead")
                        highlighted: (root.activeChart || chart).followOwnShip && DisplayConfig.lookAhead
                        onClicked: { DisplayConfig.lookAhead = true
                                     (root.activeChart || chart).followOwnShip = true
                                     followFlyout.close() }
                    }
                }
            }
        }
        MuiTool {
            text: "☰"; ToolTip.text: qsTr("Canvas display options")
            onClicked: muiBar.canvasOptionsRequested()
        }
    }

    // Drag the bar within its parent (the chart overlay), like the master bar.
    DragHandler {
        target: muiBar
        xAxis.minimum: 0
        xAxis.maximum: muiBar.parent ? muiBar.parent.width - muiBar.width : 0
        yAxis.minimum: 0
        yAxis.maximum: muiBar.parent ? muiBar.parent.height - muiBar.height : 0
    }
}
