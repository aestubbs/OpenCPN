/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

/**
 * \file
 *
 * Implement macos_titlebar.h using AppKit. A QWindow is an NSView on macOS
 * (QWindow::winId() is the NSView*), so we reach its NSWindow and add an
 * NSTitlebarAccessoryViewController anchored to the right (trailing) edge,
 * hosting a borderless NSButton with the system "sidebar.right" symbol. The
 * button's action toggles the AppController drawer state, and we reflect the
 * drawer state back onto the button via a Qt signal connection.
 */

#import <AppKit/AppKit.h>

#include <QObject>
#include <QWindow>

#include "app_controller.h"
#include "macos_titlebar.h"

// Obj-C target: forwards the button action to the C++ AppController.
@interface OcpnDrawerToggleTarget : NSObject
@property(nonatomic, assign) ocpn::qtui::AppController* app;
- (void)toggle:(id)sender;
@end

@implementation OcpnDrawerToggleTarget
- (void)toggle:(id)sender {
  (void)sender;
  if (self.app) self.app->toggleHud();
}
@end

namespace ocpn::qtui {

bool installTitlebarToggle(QWindow* window, AppController* app) {
  if (!window || !app) return false;
  NSView* view = reinterpret_cast<NSView*>(window->winId());
  if (!view) return false;
  NSWindow* win = [view window];
  if (!win) return false;

  // Kept alive for the process lifetime (one-time install).
  OcpnDrawerToggleTarget* target = [[OcpnDrawerToggleTarget alloc] init];
  target.app = app;

  NSButton* button = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 40, 24)];
  button.bordered = NO;
  button.imagePosition = NSImageOnly;
  button.target = target;
  button.action = @selector(toggle:);
  button.toolTip = @"Vessel data";
  if (@available(macOS 11.0, *)) {
    button.image = [NSImage imageWithSystemSymbolName:@"sidebar.right"
                             accessibilityDescription:@"Vessel data"];
  }
  if (!button.image) button.title = @"❯";  // fallback glyph

  // Reflect drawer state on the control (selected tint when open).
  QObject::connect(app, &AppController::hudExpandedChanged, app,
                   [button, app]() { button.state = app->hudExpanded()
                                         ? NSControlStateValueOn
                                         : NSControlStateValueOff; });

  NSTitlebarAccessoryViewController* vc =
      [[NSTitlebarAccessoryViewController alloc] init];
  vc.view = button;
  vc.layoutAttribute = NSLayoutAttributeRight;  // trailing edge of the titlebar
  [win addTitlebarAccessoryViewController:vc];
  return true;
}

}  // namespace ocpn::qtui
