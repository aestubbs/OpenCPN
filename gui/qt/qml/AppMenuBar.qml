/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

import QtQuick
import Qt.labs.platform as Platform

// --- NATIVE application menu bar (macOS global bar at the top of the SCREEN) ---
// Qt.labs.platform renders the native menu. The menu CONTENT is not defined
// here -- it is built from the single shared MenuModel (see MenuModel.qml), so
// the in-window bar (AppMenuBarInWindow.qml) and this one always match. This
// file is just the native presentation: it turns each model entry into a
// Platform.Menu / Platform.MenuItem / Platform.MenuSeparator.
Platform.MenuBar {
    id: bar

    // The single source of truth (MenuModel), wired by the shell (Main.qml).
    property var menuModel

    readonly property Component _menuComp: Component { Platform.Menu {} }
    readonly property Component _itemComp: Component { Platform.MenuItem {} }
    readonly property Component _sepComp: Component { Platform.MenuSeparator {} }
    // Restoring binding for a checkable item's tick: clicking a native item
    // toggles `checked` imperatively, which would drop a plain binding; a
    // Binding object re-asserts it so the tick keeps tracking the source.
    readonly property Component _bindingComp: Component { Binding { } }

    function _build() {
        if (!menuModel)
            return
        var menus = menuModel.menus
        for (var i = 0; i < menus.length; ++i) {
            var mdef = menus[i]
            var menu = _menuComp.createObject(bar, { title: mdef.title })
            bar.addMenu(menu)
            for (var j = 0; j < mdef.items.length; ++j) {
                var it = mdef.items[j]
                if (it.separator) {
                    menu.addItem(_sepComp.createObject(menu))
                    continue
                }
                var mi = _itemComp.createObject(menu, {
                    text: it.text,
                    shortcut: it.shortcut !== undefined ? it.shortcut : "",
                    checkable: it.checkable === true
                })
                if (it.checkable && it.checked)
                    _bindingComp.createObject(mi, {
                        target: mi, property: "checked", value: Qt.binding(it.checked)
                    })
                if (it.triggered)
                    mi.triggered.connect(it.triggered)
                menu.addItem(mi)
            }
        }
    }

    Component.onCompleted: _build()
}
