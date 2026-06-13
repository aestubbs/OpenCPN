/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "comm_priorities_model.h"

#include "model/comm_bridge.h"

namespace ocpn::qtui {

namespace {
// Map indices follow CommBridge::GetPriorityMaps() ordering.
const char* kCategoryKeys[] = {"position", "velocity", "heading",
                               "variation", "satellites"};
}  // namespace

CommPrioritiesModel::CommPrioritiesModel(QObject* parent) : QObject(parent) {}

QStringList CommPrioritiesModel::categories() const {
  return {tr("Position"), tr("Speed / Course"), tr("Heading"),
          tr("Magnetic variation"), tr("Satellites")};
}

QStringList CommPrioritiesModel::sourcesFor(int category) const {
  const auto maps = CommBridge::GetInstance().GetPriorityMaps();
  if (category < 0 || category >= static_cast<int>(maps.size())) return {};
  return QString::fromStdString(maps[category])
      .split('|', Qt::SkipEmptyParts);
}

int CommPrioritiesModel::activeIndex(int category) const {
  if (category < 0 || category > 4) return -1;
  const PriorityContainer& pc =
      CommBridge::GetInstance().GetPriorityContainer(kCategoryKeys[category]);
  return pc.active_priority;
}

void CommPrioritiesModel::move(int category, int index, int delta) {
  auto maps = CommBridge::GetInstance().GetPriorityMaps();
  if (category < 0 || category >= static_cast<int>(maps.size())) return;
  QStringList items =
      QString::fromStdString(maps[category]).split('|', Qt::SkipEmptyParts);
  const int to = index + delta;
  if (index < 0 || index >= items.size() || to < 0 || to >= items.size())
    return;
  items.swapItemsAt(index, to);
  maps[category] = items.join('|').toStdString();
  CommBridge::GetInstance().UpdateAndApplyMaps(maps);
  emit changed();
}

void CommPrioritiesModel::clearAll() {
  // wx PriorityDlg::OnClearClick: apply five empty maps; the bridge
  // relearns sources as data arrives.
  CommBridge::GetInstance().UpdateAndApplyMaps({"", "", "", "", ""});
  emit changed();
}

void CommPrioritiesModel::refresh() { emit changed(); }

}  // namespace ocpn::qtui
