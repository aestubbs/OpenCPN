# Agent rules for the OpenCPN Qt migration

Conventions for working in this repo (especially the Qt UI under `gui/qt`).
These are binding rules, not suggestions.

## Dialogs

**All dialogs must use the native system-window pattern, not in-window
sheets.** A dialog is a top-level `Window { flags: Qt.Dialog }` with a real OS
title bar — the same pattern as `objectQueryWindow`, `optionsWindow` and
`routeDetailsDialog` in `gui/qt/qml/Main.qml`.

- Do **not** use QtQuick Controls `Dialog`/`Popup` (in-window overlay
  "sheets"). They have no native title bar and don't read as a system dialog.
- Open with `show(); raise(); requestActivate()`; set `modality`
  (`Qt.ApplicationModal` for edit dialogs). Close with `close()`.
- Use a `DialogButtonBox` (`Ok`/`Cancel`/`Close`) for the action buttons.
- The native QtQuick Controls style is active (`QQuickStyle::setStyle("macOS")`
  on macOS, set in `gui/qt/main.cpp`). Build dialog content from **standard
  Controls** — `Label`, `TextField`, `ItemDelegate`, `Frame`, `GridLayout`,
  `CheckBox`, etc. — with proper margins/spacing. Do not hand-roll widgets out
  of `Rectangle` + `MouseArea` when a standard Control exists.

When converting an existing in-window `Dialog` to this pattern, move OK/Cancel
to a `DialogButtonBox` and replace `open()`/`onAccepted` with `show()` + a
button-box handler that applies then `close()`s.
