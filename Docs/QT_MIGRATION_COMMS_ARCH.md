# Comms layer — target architecture

> Status: **design**, agreed 2026-05-18. Governs the remainder of task **P1.5**
> (see [`QT_MIGRATION_TASKS.md`](./QT_MIGRATION_TASKS.md) and
> [`QT_MIGRATION_COMMS_PLAN.md`](./QT_MIGRATION_COMMS_PLAN.md)). The first two
> rewrites — P1.5a (`n2k_net`) and P1.5e (`n0183_serial`) — were done as
> standalone drivers; they revealed the common shape captured here and are
> later refactored onto it.

## 1. The shape of the problem

OpenCPN streams navigation data from many **sources** onto an internal
**message bus**. Sources differ only in two independent dimensions:

- **Medium** — UART, TCP, UDP, CAN bus, WebSocket, …
- **Wire protocol** — NMEA 0183, NMEA 2000 (raw CAN, or an Actisense/YDEN/
  SeaSmart/MiniPlex gateway serialization), SignalK, …

The legacy drivers fuse medium + protocol + lifecycle into one class per
combination — so the code is an N×M grid that grew organically, each cell
re-deriving reconnect, watchdog and statistics logic slightly differently.

## 2. The insight: everything is frames

A byte-stream medium does not carry "bytes" to the application — it carries
**frames** (NMEA 0183 sentences, Actisense packets) that happen to need
*framing* — finding the boundaries in a buffered byte stream. A frame-native
medium (CAN, Modbus, a WebSocket message) delivers the frame pre-built.

So there is **one** pipeline shape, and the transport→decoder contract is
always *a stream of frames*. "Byte-stream vs frame-native" reduces to a single
question: does this medium need a **Framer**, or a pass-through.

## 3. Components

```
┌────────────┐  bytes/frames ┌─────────┐  frames  ┌──────────────┐ NavMsg ┌────────────┐
│ CommTransport│ ───────────▶ │  Framer  │ ───────▶ │ ProtocolDecoder│ ─────▶ │ CommDriver │─▶ listener
└────────────┘               └─────────┘          └──────────────┘        └────────────┘
   media I/O                  boundary finding       frame → message        lifecycle / glue
```

### 3.1 `CommTransport` — the media adaptor
A `QObject` wrapping exactly one Qt I/O class. Interface:
`Open()`, `Close()`, `Write(QByteArray)`; signals `DataReceived(QByteArray)`,
`Connected()`, `Disconnected()`, `ErrorOccurred(QString)`. Nothing protocol-
aware. Implementations:

| Transport | Qt class | Notes |
|---|---|---|
| `SerialTransport` | `QSerialPort` | baud/parity config |
| `TcpClientTransport` | `QTcpSocket` | also GPSD |
| `TcpServerTransport` | `QTcpServer` + `QTcpSocket` | listen/accept |
| `UdpTransport` | `QUdpSocket` | unicast / multicast / broadcast |
| `CanTransport` | `QCanBusDevice` | frame-native |
| `WebSocketTransport` | `QWebSocket` | frame-native (messages) |

### 3.2 `Framer` — boundary finding
Stateful; buffers across `DataReceived` chunks. `Feed(QByteArray) →
QList<QByteArray>` (zero or more complete frames). Implementations:
`LineFramer` (NMEA 0183, terminator-delimited), `N2kGatewayFramer` (the
Actisense/YDEN/SeaSmart/MiniPlex format detection + ESC/STX/ETX framing),
`PassThroughFramer` (frame-native media — each input *is* a frame).

### 3.3 `ProtocolDecoder` — the conversion layer
`frame → NavMsg` (and the reverse, encode, for TX). May hold protocol state
(NMEA 2000 fast-message reassembly). No transport I/O of its own — a decoder
works the same whatever medium its frames arrived over. Implementations:
`Nmea0183Decoder`, `N2kDecoder`, `SignalKDecoder`.

Wire data is `QByteArray` throughout — frames, framer output, decoder
in/out. Qt-idiomatic types are used across the pipeline; a Qt dependency is
welcome (the goal is a fully Qt application).

### 3.4 `CommDriver` — lifecycle & glue
The generic driver: a `QObject` subclassing `AbstractCommDriver` (so the
existing registry / `DriverListener` / plugin-ABI boundary is unchanged).
Owns a transport + framer + decoder and provides, **written once**:
`Open`/`Close`; reconnect on a `QTimer`; the no-data watchdog `QTimer`;
`DriverStats`; and the wiring
`transport.DataReceived → framer.Feed → decoder.Decode → listener.Notify`.

A concrete driver is now a *pairing*, built by the factory from
`ConnectionParams` — e.g. `CommDriver(SerialTransport, LineFramer,
Nmea0183Decoder)`. N×M drivers collapse to **N transports + M decoders +
a handful of framers + 1 driver**.

## 4. Why this is more reliable and simpler

- Reconnect / watchdog / stats exist in **one** tested place, not re-derived
  per driver (the legacy divergence was a latent bug source).
- A new medium = one `CommTransport`; a new protocol = one decoder (+ framer).
- Decoders and framers have no transport I/O of their own → they can be
  exercised against captured frame logs with no hardware.
- Future media (Modbus, BLE, …) drop in without touching existing code.

## 5. Quirks that do not fully generalise

Handled as transport configuration or thin specialisation, not by bending the
core interfaces: TCP **server** mode (`TcpServerTransport`), UDP **multicast/
broadcast** (`UdpTransport` options), the **GPSD** `?WATCH` handshake (a TCP
transport option), and **Garmin USB** (a genuinely separate beast — kept as a
special-case driver, not forced into the pipeline).

## 6. Migration sequence

1. **P1.5i** — build the framework: `CommTransport` + the transport impls
   needed so far, `Framer` + `LineFramer`/`PassThroughFramer`,
   `ProtocolDecoder` interface, generic `CommDriver`.
2. Port the remaining drivers onto it as transport+framer+decoder triples:
   **P1.5b** (`n0183_net`), **P1.5d** (`n2k_serial`), **P1.5c**
   (`signalk_net`), **P1.5g** (`n2k_socketcan`).
3. **P1.5j** — refactor the already-done P1.5a (`n2k_net`) and P1.5e
   (`n0183_serial`) onto the framework, retiring their bespoke code.
4. Decoders/framers get unit tests as they are extracted (feeds P0.7-style
   coverage and the testability goal in §4).
