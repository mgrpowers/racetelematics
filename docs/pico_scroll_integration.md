# Pico Scroll USB Bridge (v1)

This document defines the first hardware test path from `racecontroller` telemetry to a Pimoroni Pico Scroll Pack (17x7 LEDs + 4 buttons) over USB serial.

## Goal

- Keep telemetry source unchanged (`telemetry_sim` now, Dirt Rally later).
- Send telemetry from host to Pico over USB CDC serial.
- Start with a compact race page on 17x7.
- Reserve 4 buttons for menu/settings navigation.

## Host Bridge

Build:

- `make pico`

Run:

- `./pico_scroll_bridge --serial /dev/tty.usbmodemXXXX --hz 20 -v`

Options:

- `--udp-port <n>` telemetry UDP source (default `5100`)
- `--baud <n>` serial speed (default `115200`)
- `--hz <n>` serial update rate cap (default `20`)
- `--dry-run` print outbound frames without opening serial

## Windows Build (MSYS2 / pacman)

Use MSYS2 on Windows to get `pacman`, GCC, `make`, and SDL2.

1. Install MSYS2:
   - https://www.msys2.org/
2. Open **MSYS2 UCRT64** terminal.
3. Run first-time updates:
   - `pacman -Syu`
   - close/reopen terminal if prompted, then run `pacman -Syu` again.
4. Install toolchain and SDL2:
   - `pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-SDL2`
5. Build from repo root:
   - `make`
   - `make pico`
   - `make sim`

Generated binaries:

- `racecontroller.exe`
- `pico_scroll_bridge.exe`
- `telemetry_sim.exe`

Windows serial example:

- `./pico_scroll_bridge.exe --serial COM7 --hz 20 -v`

## Pico Receiver Script

Added script:

- `pico_scroll_receiver.py`

What it does:

- Reads `T,...` telemetry lines from USB serial stdin.
- Renders a compact race page on 17x7:
  - top row RPM bar
  - middle speed band + center gear glyph
  - lower brake/accel bars
  - bottom shift/rumble indicator
- Supports local menu page and emits button events back to host:
  - `BTN,A,DOWN|UP`
  - `BTN,B,DOWN|UP`
  - `BTN,X,DOWN|UP`
  - `BTN,Y,DOWN|UP`

### Flash / Run (MicroPython)

1. Flash MicroPython (or Pimoroni MicroPython UF2) to the Pico.
2. Copy `pico_scroll_receiver.py` to the Pico as `main.py`.
3. Reboot Pico; it should print `PICO_SCROLL_READY` on USB serial.
4. Start host telemetry + bridge (see checklist below).

If your board uses different GPIO wiring, adjust constants at top of the script:

- `I2C_SDA_PIN`, `I2C_SCL_PIN`
- `BTN_PINS`

## Serial Protocol (host -> pico)

Line-based ASCII, one frame per line:

- `T,speed,rpm,rpm_max,gear,brake,accel,rumble,abs,tc,surface,shift`

Example:

- `T,184,7320,9000,4,0,31,54,0,0,0,118`

Notes:

- Numeric fields are unsigned integers.
- `shift` is `0-255`.
- Update rate should be 10-30 Hz on USB serial for stable rendering.

## Serial Protocol (pico -> host, optional)

Pico can emit button events back to host in text lines:

- `BTN,A,DOWN`
- `BTN,A,UP`
- `BTN,B,DOWN`
- `BTN,X,DOWN`
- `BTN,Y,DOWN`

The current host bridge logs inbound lines when `--verbose` is enabled.

## Suggested 17x7 Layout (first pass)

The matrix is tight, so split the display into simple channels:

- **Top row (y=0):** RPM bar graph across all 17 columns.
- **Middle rows (y=1..4):** speed bar or gear indicator (single large digit).
- **Bottom rows (y=5..6):** brake/accel split bars and shift warning blink.

This avoids dense text rendering and keeps motion legible at speed.

## Suggested Button Mapping

- `A`: toggle race/menu page
- `B`: next menu item
- `X`: decrease selected setting
- `Y`: increase selected setting

## Bring-up Checklist

1. Start telemetry source:
   - `./telemetry_sim -v`
2. Connect Pico via USB and note serial port.
3. Start bridge:
   - `./pico_scroll_bridge --serial /dev/tty.usbmodemXXXX --hz 20 -v`
4. Confirm host prints:
   - `RX: ...` telemetry
   - `TX: T,...` outbound frame lines
5. Confirm Pico receives line stream and updates LEDs.

### Platform serial notes

- macOS: prefer `/dev/cu.usbmodemXXXX` for host serial clients.
- Linux: usually `/dev/ttyACM0` or `/dev/ttyUSB0`.
- Windows: use `COMx` (for example `COM7`).
