# opencpn-qt macOS acceptance pass (P3.21)

This is the gate that must close before the parallel wx build is retired
(**P3.11** source removal → **P3.12** keyword cleanup → Windows CI → Phases
4–6). Run it on macOS against the current `opencpn-qt` build; tick each row.
The point is a deliberate human sign-off that the Qt edition is at feature
parity for day-to-day use — not pixel-identical to wx (style divergence is
intentional; see [[qt-chart-verification-no-image-diff]]).

## Pre-verified by automated smoke (2026-06-13, build at `ad939bdf6`)

These were checked headlessly (launch + `OCPN_QT_GRAB`) and need no manual
repeat unless something regresses:

- [x] App launches clean — no crash over a 20 s window, no QML errors in the log.
- [x] Vector charts render — o-charts OSENC cells (Solent/Southampton Water),
      soundings, buoys, lights, depth areas, place labels, compass rose,
      data HUD, toolbar, time bar all present.
- [x] Translations compile + load — `de` catalog loads under a German locale;
      273 finished strings (`lconvert` round-trip).
- [x] Persisted viewport restores on launch (opened on the last view).
- [x] arm64 / Raspberry Pi build links in CI (separate from this macOS gate).

## Manual checklist (needs eyes)

### Charts
- [ ] NOAA ENC `.000` cells load and render (a US area).
- [ ] o-charts encrypted set decrypts + renders (oexserverd present).
- [ ] Quilt has no gaps/overlaps across 3–4 zoom steps in a multi-cell area.
- [ ] Day / Dusk / Night palette switch all correct.
- [ ] Soundings + text de-clutter toggles behave; SCAMIN culls as expected.
- [ ] Overzoom indication + depth-unit legend + grid display.

### Navigation
- [ ] Own-ship icon, COG/SOG vector, heading predictor.
- [ ] North-Up / Course-Up / Head-Up (+ look-ahead); click-compass toggle.
- [ ] Auto-follow; manual pan/zoom is smooth (no hitch).

### AIS
- [ ] Targets render with correct colour/state; CPA/TCPA + danger display.
- [ ] AIS Target List opens, sorts, centres on a target; MMSI properties.

### Routes / Marks / Tracks
- [ ] Create / edit / delete route; insert / append / split; reverse; Zero XTE.
- [ ] Mark editor (icon, ranging rings, SCAMIN); confirm-delete.
- [ ] Track recording + day labels; GPX import/export.
- [ ] Send-to-GPS and Send-to-Peer.

### Tides & Currents
- [ ] Tide/current stations show; tide graph; the time bar drives the display.

### Connections (comms)
- [ ] Serial / TCP(client+listen) / UDP / GPSD / SignalK connections editor.
- [ ] A live NMEA 0183 + NMEA 2000 feed updates own-ship + AIS.
- [ ] Data Monitor shows traffic; connection priorities editor.

### UI shell
- [ ] Menu bar tiers (Navigate / View / AIS / Tools / Help) all act.
- [ ] Context menus (chart / route / mark / AIS / track) hit-test correctly.
- [ ] Options, About, Data Monitor open as native windows.
- [ ] Language switch (Options → UI) applies on restart.

### o-charts
- [ ] Shop login succeeds (the result-5 fix); purchased sets list + install.
- [ ] Decryption-helper banner shows the daemon version.

## Sign-off

- Tester: ____________________   Date: __________
- Result: ☐ PASS (proceed to P3.11 wx removal)  ☐ FAIL (file defects below)
- Defects / notes:
