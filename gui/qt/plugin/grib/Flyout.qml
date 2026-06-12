import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// The GRIB flyout (hold / right-click the 🌬 tool): a mini chip toolbar
// in the MUI-flyout style -- just toggles. Heavyweight options live in
// Settings (⚙); time lives on the chart time bar.
Item {
    property var pluginContext: null

    // Click-away dismissal.
    MouseArea {
        anchors.fill: parent
        visible: flyPanel.visible
        onPressed: (m) => { pluginContext.controlsVisible = false
                            m.accepted = false }
    }

    Rectangle {
        id: flyPanel
        visible: pluginContext && pluginContext.controlsVisible
        x: 76          // immediately right of the master toolbar
        y: 16 + 5 * 46  // roughly level with the plugin tools
        width: chipCol.implicitWidth + 8
        height: chipCol.implicitHeight + 8
        radius: 6
        color: Qt.rgba(0.93, 0.93, 0.95, 0.95)
        border.color: Qt.rgba(0, 0, 0, 0.35)
        border.width: 1
        scale: visible ? 1.0 : 0.85
        opacity: visible ? 1.0 : 0.0
        Behavior on scale { NumberAnimation { duration: 110
                                              easing.type: Easing.OutBack } }
        Behavior on opacity { NumberAnimation { duration: 90 } }

        // The same chip styling as the toolbars.
        component Chip: ToolButton {
            id: chip
            font.pointSize: 13
            implicitWidth: 40
            implicitHeight: 40
            ToolTip.visible: hovered && ToolTip.text.length > 0
            ToolTip.delay: 400
            background: Rectangle {
                radius: 4
                color: (chip.checked || chip.down || chip.highlighted)
                           ? Qt.rgba(0.30, 0.47, 0.75, 0.92)
                       : chip.hovered ? Qt.rgba(0.78, 0.84, 0.93, 0.9)
                       : Qt.rgba(0.87, 0.88, 0.91, 0.9)
            }
            contentItem: Text {
                text: chip.text
                font: chip.font
                color: (chip.checked || chip.down || chip.highlighted)
                           ? "white" : "#1b1d21"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        RowLayout {
            id: chipCol
            anchors.centerIn: parent
            spacing: 2

            // Per-type toggles (only types in the file). Short glyphs.
            Repeater {
                model: pluginContext ? pluginContext.dataTypes : []
                delegate: Chip {
                    required property var modelData
                    visible: modelData.available
                    checkable: true
                    checked: modelData.shown
                    text: { switch (modelData.key) {
                        case "wind": return "🌬"
                        case "pressure": return "⊜"
                        case "waves": return "🌊"
                        case "current": return "➳"
                        case "gust": return "💨"
                        case "rain": return "🌧"
                        case "cloud": return "☁"
                        case "airtemp": return "🌡"
                        case "seatemp": return "🌡̱"
                        case "cape": return "⚡"
                        case "refl": return "📡"
                        case "humid": return "💧"
                        default: return modelData.label[0] } }
                    ToolTip.text: modelData.label
                    onClicked: pluginContext.setTypeShown(modelData.key,
                                                          checked)
                }
            }
            Rectangle {
                Layout.fillHeight: true; width: 1
                color: Qt.rgba(0, 0, 0, 0.2)
            }
            Chip {
                checkable: true
                text: "✨"; ToolTip.text: qsTr("Particle animation")
                checked: pluginContext ? pluginContext.particles : false
                onClicked: if (pluginContext) pluginContext.particles = checked
            }
            Chip {
                checkable: true
                text: "🎯"; ToolTip.text: qsTr("Data at cursor")
                checked: pluginContext ? pluginContext.cursorPanelVisible : false
                onClicked: if (pluginContext)
                               pluginContext.cursorPanelVisible = checked
            }
            Chip {
                text: "📂"; ToolTip.text: qsTr("Open GRIB…")
                onClicked: flyDirMenu.open()
                Menu {
                    id: flyDirMenu
                    Instantiator {
                        model: pluginContext ? pluginContext.dirFiles : []
                        delegate: MenuItem {
                            id: fileItem
                            required property var modelData
                            text: modelData.name + "   " + modelData.date
                            onTriggered: {
                                pluginContext.openFile(modelData.path)
                                pluginContext.controlsVisible = false
                            }
                            // Layer this file OVER the loaded forecast
                            // (wx multi-file: e.g. waves + wind files
                            // merge into one record set).
                            ToolButton {
                                anchors.right: parent.right
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                width: 26; height: 26
                                text: "+"
                                visible: pluginContext
                                         && pluginContext.fileName.length > 0
                                ToolTip.text: qsTr("Add to loaded forecast")
                                ToolTip.visible: hovered
                                onClicked: {
                                    flyDirMenu.close()
                                    pluginContext.addFile(fileItem.modelData.path)
                                    pluginContext.controlsVisible = false
                                }
                            }
                        }
                        onObjectAdded: (i, o) => flyDirMenu.insertItem(i, o)
                        onObjectRemoved: (i, o) => flyDirMenu.removeItem(o)
                    }
                    MenuSeparator { }
                    MenuItem {
                        text: qsTr("Browse…")
                        onTriggered: gribFlyFileDialog.open()
                    }
                }
            }
            Chip {
                text: "⚙"; ToolTip.text: qsTr("GRIB settings…")
                onClicked: {
                    pluginContext.controlsVisible = false
                    optionsWindow.openPluginPrefs("GRIB")
                }
            }
        }
    }
    FileDialog {
        id: gribFlyFileDialog
        nameFilters: [qsTr("GRIB files (*.grb *.grb2 *.grib *.grib2 *.bz2 *.gz)"),
                      qsTr("All files (*)")]
        onAccepted: if (pluginContext) pluginContext.openFile(selectedFile)
    }
}
