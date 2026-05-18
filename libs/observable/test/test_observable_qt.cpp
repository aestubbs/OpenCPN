/*************************************************************************
 *
 * Copyright (C) 2026 OpenCPN Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.
 **************************************************************************/

/**
 * \file
 *
 * Unit tests for observable_qt.h (task P1.1f).
 */

#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include "observable_qt.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "FAIL: " << #cond << "  (" << __FILE__ << ":" << __LINE__  \
                << ")\n";                                                    \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

/** Pump the event loop until done() is true or timeout (ms) elapses. */
void PumpUntil(const std::function<bool()>& done, int timeout_ms = 1000) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
}

/** A bare Notify() reaches a listener with the right string payload. */
void TestBasicNotify() {
  EventVarQt var;
  std::string received;
  ObsConnection conn(var.Key(),
                     [&](const ObsData& d) { received = d.string; });
  var.Notify(std::string("hello"));
  PumpUntil([&] { return !received.empty(); });
  CHECK(received == "hello");
}

/** Every listener on a key is notified. */
void TestMultipleListeners() {
  EventVarQt var;
  int count = 0;
  ObsConnection a(var.Key(), [&](const ObsData&) { ++count; });
  ObsConnection b(var.Key(), [&](const ObsData&) { ++count; });
  var.Notify();
  PumpUntil([&] { return count >= 2; });
  CHECK(count == 2);
}

/** A destroyed ObsConnection no longer receives notifications. */
void TestRaiiDisconnect() {
  EventVarQt var;
  int count = 0;
  {
    ObsConnection conn(var.Key(), [&](const ObsData&) { ++count; });
    var.Notify();
    PumpUntil([&] { return count >= 1; });
    CHECK(count == 1);
  }
  var.Notify();
  PumpUntil([] { return false; }, 100);  // drain; nothing should arrive
  CHECK(count == 1);
}

/** All payload fields survive a round trip. */
void TestPayload() {
  EventVarQt var;
  auto payload = std::make_shared<const int>(42);
  int got_num = 0;
  std::string got_str;
  std::shared_ptr<const void> got_ptr;
  ObsConnection conn(var.Key(), [&](const ObsData& d) {
    got_num = d.num;
    got_str = d.string;
    got_ptr = d.shared_ptr;
  });
  var.Notify(payload, "data", 7);
  PumpUntil([&] { return got_num != 0; });
  CHECK(got_num == 7);
  CHECK(got_str == "data");
  CHECK(got_ptr == payload);
  CHECK(got_ptr && *std::static_pointer_cast<const int>(got_ptr) == 42);
}

/** A moved-from connection delivers exactly once via its new owner. */
void TestMoveSemantics() {
  EventVarQt var;
  int count = 0;
  ObsConnection a(var.Key(), [&](const ObsData&) { ++count; });
  ObsConnection b = std::move(a);
  var.Notify();
  PumpUntil([&] { return count >= 1; });
  CHECK(count == 1);
}

/** Notify() from a worker thread is delivered on the listener's thread. */
void TestCrossThread() {
  EventVarQt var;
  std::atomic<int> count{0};
  QThread* main_thread = QCoreApplication::instance()->thread();
  ObsConnection conn(var.Key(), [&](const ObsData&) {
    CHECK(QThread::currentThread() == main_thread);
    ++count;
  });

  QThread worker;
  EventVarQt* pvar = &var;
  QObject::connect(&worker, &QThread::started, [pvar] { pvar->Notify(); });
  worker.start();
  PumpUntil([&] { return count.load() >= 1; });
  worker.quit();
  worker.wait();
  CHECK(count.load() == 1);
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  TestBasicNotify();
  TestMultipleListeners();
  TestRaiiDisconnect();
  TestPayload();
  TestMoveSemantics();
  TestCrossThread();

  if (g_failures == 0) {
    std::cout << "All observable_qt tests passed.\n";
    return 0;
  }
  std::cerr << g_failures << " observable_qt test(s) failed.\n";
  return 1;
}
