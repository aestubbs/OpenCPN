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

// Shared "Vector chart detail" checklist -- used by BOTH the quick
// pull-out drawer (MUIBar canvas options) and the Options > Charts >
// Vector Display tab, so the two surfaces can never drift (the bug that
// left the drawer showing the old 4-toggle list). Soundings is a live
// ChartCanvas toggle; the rest bind ChartConfig (re-decode via
// applyChartConfig). Mirrors the wx "Vector Chart Display" detail list.
// Extracted from Main.qml (P3.17); `chart` resolves via the context chain.
ColumnLayout {
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
