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
import QtQuick.Window

// --- IN-WINDOW application menu bar (Linux / Raspberry Pi / Windows) ---
// The native Qt.labs.platform bar (AppMenuBar.qml) can't render on a plain
// window manager / EGLFS / Wayland, so those platforms draw this QtQuick.
// Controls bar at the top of the WINDOW. Like the native bar, the CONTENT is
// not defined here -- it is built from the single shared MenuModel (see
// MenuModel.qml). This file is just the in-window presentation.
//
// Each item becomes a MenuItem backed by an Action (the Action carries the text
// + keyboard shortcut, and shows the shortcut hint in the menu). Checkable
// items get a restoring Binding on the Action's `checked`: QtQuick.Controls
// toggles `checked` imperatively on click, which would drop a plain binding and
// stop the tick tracking external changes (toolbar, Options); the Binding
// re-asserts it after every toggle.
MenuBar {
    id: bar

    // The single source of truth (MenuModel), wired by the shell (Main.qml).
    property var menuModel

    readonly property Component _menuComp: Component { Menu {} }
    readonly property Component _itemComp: Component { MenuItem {} }
    readonly property Component _sepComp: Component { MenuSeparator {} }
    readonly property Component _actionComp: Component { Action {} }
    readonly property Component _bindingComp: Component { Binding {} }

    function _build() {
        if (!menuModel)
            return
        var menus = menuModel.menus
        for (var i = 0; i < menus.length; ++i) {
            var mdef = menus[i]
            var menu = _menuComp.createObject(bar, { title: mdef.title })
            for (var j = 0; j < mdef.items.length; ++j) {
                var it = mdef.items[j]
                if (it.separator) {
                    // Create with no visual parent; Menu.addItem reparents it
                    // into the menu's content (else "not placed in scene").
                    menu.addItem(_sepComp.createObject(null))
                    continue
                }
                var action = _actionComp.createObject(bar, {
                    text: it.text,
                    shortcut: it.shortcut !== undefined ? it.shortcut : "",
                    checkable: it.checkable === true
                })
                if (it.checkable && it.checked)
                    _bindingComp.createObject(action, {
                        target: action, property: "checked", value: Qt.binding(it.checked)
                    })
                if (it.triggered)
                    action.triggered.connect(it.triggered)
                menu.addItem(_itemComp.createObject(null, { action: action }))
            }
            bar.addMenu(menu)
        }
    }

    Component.onCompleted: _build()
}
