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
 * ObjArray<T> -- a minimal stand-in for wxWidgets' WX_DECLARE_OBJARRAY used by
 * the de-wx'd tide engine. It owns its elements by pointer (Add() takes
 * ownership; Clear()/dtor delete them) and exposes the small slice of the
 * wxObjArray API the tide code uses, so the call sites (.Add / .Item / [] /
 * .GetCount / .Clear) port over unchanged.
 */

#ifndef OCPN_TIDES_OBJ_ARRAY_H_
#define OCPN_TIDES_OBJ_ARRAY_H_

#include <cstddef>
#include <vector>

template <class T>
class ObjArray {
public:
  ObjArray() = default;
  ~ObjArray() { Clear(); }

  // Non-copyable: it owns its elements (matches wxObjArray ownership intent).
  ObjArray(const ObjArray&) = delete;
  ObjArray& operator=(const ObjArray&) = delete;

  /** Append, taking ownership of the pointer (wxObjArray::Add(T*)). */
  void Add(T* p) { m_items.push_back(p); }

  size_t GetCount() const { return m_items.size(); }

  T& Item(size_t i) { return *m_items[i]; }
  const T& Item(size_t i) const { return *m_items[i]; }
  T& operator[](size_t i) { return *m_items[i]; }
  const T& operator[](size_t i) const { return *m_items[i]; }

  void Clear() {
    for (T* p : m_items) delete p;
    m_items.clear();
  }

private:
  std::vector<T*> m_items;
};

#endif  // OCPN_TIDES_OBJ_ARRAY_H_
