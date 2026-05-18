# P1.5 — Comms layer: Qt-native transport strategy

> Status: **plan** — companion to [`QT_MIGRATION_TASKS.md`](./QT_MIGRATION_TASKS.md)
> task **P1.5** and its sub-tasks. Supersedes the driver-by-driver framing of
> the original P1.5; the n2k_net rewrite already done is recorded in
> [`QT_MIGRATION_N2K_NET_PLAN.md`](./QT_MIGRATION_N2K_NET_PLAN.md).

## 1. Goal

Migrate every comm driver to a **Qt-native transport class** — not merely
"drop `wxEvtHandler`", but adopt the correct Qt class for each transport. The
payoff is structural: vendored libraries and platform `#ifdef` code are
*deleted*, not ported, which is exactly what the migration plan
([`QT_MIGRATION.md`](./QT_MIGRATION.md) §3) calls for.

What Qt does **not** replace: the maritime framing/decode — Actisense / YDEN /
SeaSmart / MiniPlex gateway formats, NMEA 0183 sentence parsing, NMEA 2000
fast-message reassembly. That logic is OpenCPN-specific and stays in every
driver. **Qt simplifies the transport; never the protocol.**

## 2. Two transport shapes

Qt's I/O classes fall into two families, and the driver structure differs
accordingly:

| Shape | Qt classes | Arrival signal | Read unit |
|---|---|---|---|
| Byte-stream (`QIODevice`) | `QSerialPort`, `QTcpSocket`, `QUdpSocket` | `readyRead()` | bytes |
| Frame / message-oriented | `QCanBusDevice` (QtSerialBus), `QWebSocket` | `framesReceived()` / `textMessageReceived()` | a frame / a message |

A driver therefore = **a Qt transport `QObject` + an OpenCPN protocol
decoder**, reacting to the transport's arrival signal. No custom worker
threads, no custom `wxEvent`s, no raw sockets/ioctls.

> "N2K" is not always CAN. Only `comm_drv_n2k_socketcan` talks a real CAN bus
> (`QCanBus`). The N2K *gateway* drivers (`n2k_serial`, `n2k_net`) carry a
> proprietary serialization over a UART / TCP socket — byte-stream transports
> with N2K framing decoded on top.

## 3. Driver → Qt transport map

| Driver | Transport reality | Qt class | Retires |
|---|---|---|---|
| `comm_drv_n2k_socketcan` | real CAN bus (Linux) | `QCanBus` / `QCanBusDevice` | raw `PF_CAN` socket, `ioctl`, `Worker` thread |
| `comm_drv_n0183_serial` | NMEA 0183 over UART | `QSerialPort` | vendored `libs/serial`, `SerialIo` |
| `comm_drv_n2k_serial` | N2K via Actisense-type UART gateway | `QSerialPort` | vendored `libs/serial` |
| `comm_drv_n0183_net` | NMEA 0183 over TCP/UDP | `QTcpSocket` / `QUdpSocket` | `wxSocket` |
| `comm_drv_n2k_net` | N2K over TCP/UDP gateway | `QTcpSocket` / `QUdpSocket` | ✅ done — P1.5a |
| `comm_drv_signalk_net` | SignalK over WebSocket | `QWebSocket` | vendored `IXWebSocket` (for this driver) |
| `comm_drv_n0183_android_*` | 0183 over Bluetooth (Android) | `QBluetoothSocket` | wxQt event plumbing |
| serial port enumeration (`ser_ports.cpp`) | — | `QSerialPortInfo` | ~8 platform `#ifdef` branches |

## 4. Order of work — two reference patterns first

Two drivers are done first because **the rest are adaptations of them**:

1. **P1.5e — `comm_drv_n0183_serial` → `QSerialPort`.** The primary reference
   — the *byte-stream serial* pattern. `QSerialPort` (a `QIODevice`) replaces
   the `SerialIo` worker-thread abstraction and the vendored `libs/serial`;
   `QSerialPortInfo` replaces the `ser_ports.cpp` enumeration `#ifdef`s. Fully
   buildable and verifiable on macOS.

2. **P1.5g — `comm_drv_n2k_socketcan` → `QCanBus`.** The *frame-oriented*
   reference. `QCanBusDevice` subsumes the raw `socket(PF_CAN, SOCK_RAW,
   CAN_RAW)`, the `ioctl`/`setsockopt` setup, and the entire `Worker` read
   thread — it delivers `QCanBusFrame`s via `framesReceived()`; the N2K
   fast-message reassembly stays. Once on `QCanBus` the driver is portable
   C++/Qt (the Linux-only `PF_CAN` headers are gone) and **builds on macOS**;
   it picks its backend by name — `socketcan` on Linux for real hardware,
   `virtualcan` (shipped in the macOS Qt) for hardware-free dev/test. Only the
   real `socketcan` backend is Linux-specific.

Then, as adaptations:

3. **P1.5d — `comm_drv_n2k_serial`** — `QSerialPort` transport (pattern P1.5e)
   + N2K gateway framing.
4. **P1.5b — `comm_drv_n0183_net`** — `QtNetwork` (pattern P1.5a).
5. **P1.5c — `comm_drv_signalk_net`** — `QWebSocket`.
6. **P1.5f — Android Bluetooth driver** — `QBluetoothSocket`; deferred.

## 5. Common driver shape — the comms framework

P1.5a and P1.5e revealed the shared shape; it is now a planned architecture —
see [`QT_MIGRATION_COMMS_ARCH.md`](./QT_MIGRATION_COMMS_ARCH.md). The remaining
drivers are built as `CommTransport` + `Framer` + `ProtocolDecoder` triples on
a single generic `CommDriver`, not as standalone classes. The framework is
built first (P1.5i); P1.5a/e are refactored onto it afterwards (P1.5j).

All driver classes remain native `QObject`s with `Q_OBJECT` / `Q_SIGNALS` /
`Q_SLOTS` under `QT_NO_KEYWORDS` (established in P1.5a; relaxed by P3.12).

## 6. Build wiring

- Top-level `CMakeLists.txt`: extend the `find_package(Qt6 …)` components with
  `SerialPort`, `SerialBus` (Linux), and later `WebSockets` / `Bluetooth`.
- `model/CMakeLists.txt`: link `Qt6::SerialPort` (and `Qt6::SerialBus` on
  Linux) into `_model_src`; `AUTOMOC` is already on.

## 7. Risks

- **CAN backend coverage is platform-specific** — the real `socketcan`
  backend is Linux-only; PCAN/TinyCAN are Linux/Windows; `virtualcan` is
  available everywhere (incl. the macOS Qt). The P1.5g *driver* builds on
  macOS once it is on `QCanBus`; the real `socketcan` path is validated on
  Linux (e.g. a VM/container with the `vcan` kernel module). There is no real
  CAN-hardware path on macOS.
- **Functional verification needs hardware** — a CAN interface for P1.5g, a
  serial GPS/AIS device for P1.5e/d. Compile + run + no-crash is the limit of
  what the dev box proves; on-water/bench testing is required before release.
- **Plugin ABI** — the transport rework stays behind `AbstractCommDriver` /
  `DriverListener`; the wx plugin ABI boundary is unchanged (cf. P1.1e).
