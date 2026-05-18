# P1.5a — `CommDriverN2KNet` wxSocket → Qt rewrite

> Status: **plan** — companion to [`QT_MIGRATION_TASKS.md`](./QT_MIGRATION_TASKS.md)
> task **P1.5a**. Scope: the NMEA 2000 IP network driver only. The NMEA 0183
> net driver (`comm_drv_n0183_net`) and SignalK net driver follow as P1.5b/c.

## 1. Why this driver first

NMEA 2000 / CAN bus is the modern wiring standard on yachts and covers the
majority of installations; NMEA 0183 (VHF/AIS integration etc.) follows. It is
also the hardest of the net drivers, so it sets the pattern for the rest.

## 2. Current architecture

`CommDriverN2KNet` (`comm_drv_n2k_net.{h,cpp}`, ~2000 lines) is **single
threaded** — every wxSocket event is delivered on the main thread via the wx
event loop. There is no worker thread.

- **Base classes:** `CommDriverN2K`, `wxEvtHandler`, `DriverStatsProvider`.
- **Sockets (`wxSocket`):** `wxSocketClient` (TCP client + GPSD),
  `wxSocketServer` (TCP server), `wxDatagramSocket` (UDP rx and tx). Multicast
  joins via raw `setsockopt(IP_ADD_MEMBERSHIP)` (`MrqContainer`).
- **Timers (`wxTimer`):** `m_socket_timer` (reconnect, one-shot),
  `m_socketread_watchdog_timer` (no-data watchdog, 1 s continuous),
  `m_prodinfo_timer` (YDEN TX-capability detection, 200 ms one-shot).
  `m_stats_timer` is already a pure-C++ `PeriodicTimer` — untouched.
- **Event delivery:** a custom `CommDriverN2KNetEvent : wxEvent` carries a
  decoded payload; the read path posts it with `AddPendingEvent()` and a
  `BEGIN_EVENT_TABLE` / `Bind` routes it to `handle_N2K_MSG`. This is a
  *deferral within the main thread*, not a thread hop — it lets the socket
  handler return before the message is pushed to upper layers.
- **Protocol parsers:** `DetectFormat`, `ProcessActisense_*`, `ProcessSeaSmart`,
  `ProcessMiniPlex`, `HandleCanFrameInput`, `GetTxVector`, `MakeSimpleOutMsg`
  (~600 lines) are socket-agnostic — they read `m_circle` (`CircularBuffer`)
  and emit payloads. **They are not rewritten**; only their 6 payload-emit
  sites change.

## 3. Target architecture

`CommDriverN2KNet` becomes a native `QObject` with real signals/slots.

| Concern | wx | Qt |
|---|---|---|
| Base / event handler | `wxEvtHandler` + `DECLARE_EVENT_TABLE` | `QObject` + `Q_OBJECT` |
| TCP client / GPSD | `wxSocketClient` | `QTcpSocket` |
| TCP server | `wxSocketServer` | `QTcpServer` |
| UDP rx / tx | `wxDatagramSocket` | `QUdpSocket` |
| Multicast join | raw `setsockopt` (`MrqContainer`) | `QUdpSocket::joinMulticastGroup` |
| Socket options | `setsockopt` TCP_NODELAY / SO_SNDBUF | `QAbstractSocket::setSocketOption` |
| Timers | `wxTimer` | `QTimer` |
| Connect time | `wxDateTime` | `QDateTime` |
| Target address | `wxIPV4address` | `QString` host + `int` port / `QHostAddress` |
| Decoded-payload delivery | custom `wxEvent` + `AddPendingEvent` | `Q_SIGNALS` signal, `Qt::QueuedConnection` |

**Event delivery.** One signal `N2kMsgReceived(N2kPayloadPtr)` is emitted from
the read path and connected to slot `HandleN2kPayload` with
`Qt::QueuedConnection`. The queued connection reproduces the old
`AddPendingEvent` deferral exactly — emitted on the main thread, dispatched
later on the main thread. `N2kPayloadPtr` (`std::shared_ptr<std::vector<
unsigned char>>`) is registered with `qRegisterMetaType` so it can cross a
queued connection.

**Socket events → slots.** wxSocket's `wxSOCKET_INPUT/CONNECTION/LOST` become
`QTcpSocket::readyRead/connected/disconnected/errorOccurred`; `QTcpServer::
newConnection`; `QUdpSocket::readyRead`.

**Event loop.** `QTcpSocket`/`QUdpSocket`/`QTimer` need a running Qt event
loop. `QtEventBridge` (task P1.16) already pumps `QCoreApplication::
processEvents()` from a wx timer on the main thread, which services socket
notifiers and timers — no extra work needed.

## 4. `QT_NO_KEYWORDS`

The driver header (`comm_drv_n2k_net.h`) is included by wx-heavy translation
units (`comm_drv_factory.cpp`, `plugin_api.cpp`, `wiz_ui.cpp`). A native
`QObject` header therefore pulls Qt into TUs that also include wxWidgets. To
avoid the `signals`/`slots`/`emit` macro collision, the build defines
**`QT_NO_KEYWORDS`** globally; Qt code uses `Q_SIGNALS` / `Q_SLOTS` / `Q_EMIT`.

This is a **transitional** measure. Once the wx GUI/build path is removed
(after P3.11) there is nothing left to clash with. A new cross-cutting task
**P3.12** records the cleanup: drop `QT_NO_KEYWORDS` and restore the plain
`signals`/`slots`/`emit` keywords.

Existing Qt code converted in passing: `observable_qt` (`signals:` →
`Q_SIGNALS:`, `emit` → `Q_EMIT`).

## 5. Build wiring

- Top-level `CMakeLists.txt`: `add_compile_definitions(QT_NO_KEYWORDS)` inside
  the existing `NOT QT_ANDROID` Qt block.
- `model/CMakeLists.txt`: `_model_src` gains `AUTOMOC ON` and an explicit
  `Qt6::Core` link — `comm_drv_n2k_net.h` now declares a `Q_OBJECT` class that
  `moc` must process.

## 6. Steps

1. Build wiring (`QT_NO_KEYWORDS`, `_model_src` AUTOMOC + Qt6::Core).
2. Convert `observable_qt` to `Q_SIGNALS`/`Q_EMIT`.
3. Rewrite `comm_drv_n2k_net.h` — `QObject`, Qt member types, signal/slots.
4. Rewrite the socket/event/timer functions in `comm_drv_n2k_net.cpp`
   (ctor/dtor, `Open`/`OpenNetworkTCP`/`OpenNetworkUDP`, the socket-event and
   server-event handlers split into slots, `SendSentenceNetwork`, `Close`,
   `SetOutputSocketOptions`, the three timer handlers, `handle_N2K_MSG`).
5. Repoint the 6 payload-emit sites in the parsers to `Q_EMIT N2kMsgReceived`.
6. Build green; smoke-test the app.

## 7. Risks & verification

- **No N2K hardware in CI / dev** — functional RX/TX cannot be fully verified
  here. The change is verified to compile, link, and run without crashing;
  on-water or simulator testing against a real gateway (Actisense / YDEN /
  MiniPlex) is required before release. Flagged explicitly.
- **UDP multicast** — `joinMulticastGroup` must bind `AnyIPv4` with
  `ShareAddress | ReuseAddressHint` first; ordering matters.
- **TCP reconnect** — a `QTcpSocket` is reused across reconnects (`abort()`
  then `connectToHost()`), unlike the wx code which sometimes recreated it.
- **Parsers untouched** — `wxString`/`wxStringTokenizer` inside the parsers is
  left as-is; their de-wx is the later string sweep (P1.6), keeping this task
  scoped to sockets/events.
