import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// The GRIB flyout (hold / right-click the 🌬 tool): a row of TOGGLE chips,
// hosted in the master toolbar's animated bulge (registerToolbarFlyout). Unlike
// the colour flyout these don't rotate onto the toolbar -- each just toggles a
// feature on/off. Heavyweight options live in Settings (⚙); time lives on the
// chart time bar.
//
// Embeddable content: the root is the chip row itself (the toolbar sizes its
// bulge to it). The host sets `pluginContext`; `settingsRequested` opens the
// plugin's preferences and `closeRequested` dismisses the flyout.
RowLayout {
    id: gribFlyout
    property var pluginContext: null
    // Chip size: the host (toolbar) sets this to its own touchSize so the
    // flyout chips match the toolbar buttons. Default ~ the toolbar default.
    property real chipSize: 40
    signal settingsRequested()
    signal closeRequested()
    spacing: 2

    // Same chip styling as the toolbars.
    component Chip: ToolButton {
        id: chip
        font.pointSize: 16
        implicitWidth: gribFlyout.chipSize
        implicitHeight: gribFlyout.chipSize
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

    // Per-type toggles (only types present in the file). Short glyphs.
    Repeater {
        model: gribFlyout.pluginContext ? gribFlyout.pluginContext.dataTypes : []
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
            onClicked: gribFlyout.pluginContext.setTypeShown(modelData.key, checked)
        }
    }
    Rectangle {
        Layout.fillHeight: true; Layout.topMargin: 4; Layout.bottomMargin: 4
        width: 1
        color: Qt.rgba(0, 0, 0, 0.2)
    }
    Chip {
        checkable: true
        text: "✨"; ToolTip.text: qsTr("Particle animation")
        checked: gribFlyout.pluginContext ? gribFlyout.pluginContext.particles : false
        onClicked: if (gribFlyout.pluginContext) gribFlyout.pluginContext.particles = checked
    }
    Chip {
        checkable: true
        text: "🎯"; ToolTip.text: qsTr("Data at cursor")
        checked: gribFlyout.pluginContext ? gribFlyout.pluginContext.cursorPanelVisible : false
        onClicked: if (gribFlyout.pluginContext)
                       gribFlyout.pluginContext.cursorPanelVisible = checked
    }
    Chip {
        text: "📂"; ToolTip.text: qsTr("Open GRIB…")
        onClicked: flyDirMenu.open()
        Menu {
            id: flyDirMenu
            Instantiator {
                model: gribFlyout.pluginContext ? gribFlyout.pluginContext.dirFiles : []
                delegate: MenuItem {
                    id: fileItem
                    required property var modelData
                    text: modelData.name + "   " + modelData.date
                    onTriggered: {
                        gribFlyout.pluginContext.openFile(modelData.path)
                        gribFlyout.closeRequested()
                    }
                    ToolButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        width: 26; height: 26
                        text: "+"
                        visible: gribFlyout.pluginContext
                                 && gribFlyout.pluginContext.fileName.length > 0
                        ToolTip.text: qsTr("Add to loaded forecast")
                        ToolTip.visible: hovered
                        onClicked: {
                            flyDirMenu.close()
                            gribFlyout.pluginContext.addFile(fileItem.modelData.path)
                            gribFlyout.closeRequested()
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
        onClicked: { gribFlyout.settingsRequested(); gribFlyout.closeRequested() }
    }

    FileDialog {
        id: gribFlyFileDialog
        nameFilters: [qsTr("GRIB files (*.grb *.grb2 *.grib *.grib2 *.bz2 *.gz)"),
                      qsTr("All files (*)")]
        onAccepted: if (gribFlyout.pluginContext) gribFlyout.pluginContext.openFile(selectedFile)
    }
}
