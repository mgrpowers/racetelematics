"""
Pico Scroll Pack receiver for racecontroller telemetry.

Expected USB serial input lines from host:
  T,speed,rpm,rpm_max,gear,brake,accel,rumble,abs,tc,surface,shift

Optional USB serial output lines (button events):
  BTN,A,DOWN / BTN,A,UP
  BTN,B,DOWN / BTN,B,UP
  BTN,X,DOWN / BTN,X,UP
  BTN,Y,DOWN / BTN,Y,UP

This script uses direct I2C for the IS31FL3731 LED driver and raw GPIO
for buttons so it can run without extra Python packages.
"""

import machine
import sys
import time
import uselect

# Pico Scroll Pack defaults.
I2C_ID = 0
I2C_SDA_PIN = 4
I2C_SCL_PIN = 5
I2C_FREQ = 400000
IS31_ADDR = 0x74

WIDTH = 17
HEIGHT = 7
PWM_COUNT = 144

# Button pin guesses for Pico Scroll Pack. Adjust if needed.
BTN_PINS = {
    "A": 12,
    "B": 13,
    "X": 14,
    "Y": 15,
}

# Tuning knobs.
MAX_SPEED_KPH = 300
KPH_TO_MPH = 0.621371
MAX_SPEED_MPH = 99
DEFAULT_BRIGHTNESS = 20
MAX_BRIGHTNESS = 80
FRAME_MS = 33  # ~30 FPS local refresh
TELEM_TIMEOUT_MS = 1200
TEXT_FLIP_X = True   # set False if digits look mirrored
TEXT_FLIP_Y = False  # set True if digits look upside-down


class IS31FL3731:
    # Register addresses.
    _REG_COMMAND = 0xFD
    _REG_SHUTDOWN = 0x0A
    _REG_PICTURE = 0x01

    # Banks.
    _BANK_FUNCTION = 0x0B
    _BANK_FRAME0 = 0x00

    def __init__(self, i2c, addr=IS31_ADDR):
        self.i2c = i2c
        self.addr = addr
        self.buf = bytearray(PWM_COUNT)
        self._init_chip()

    def _write_reg(self, reg, val):
        self.i2c.writeto_mem(self.addr, reg, bytes([val]))

    def _select_bank(self, bank):
        self._write_reg(self._REG_COMMAND, bank)

    def _init_chip(self):
        # Select function bank and bring chip out of shutdown.
        self._select_bank(self._BANK_FUNCTION)
        self._write_reg(self._REG_SHUTDOWN, 0x01)
        self._write_reg(self._REG_PICTURE, self._BANK_FRAME0)

        # Enable all LEDs in frame 0.
        self._select_bank(self._BANK_FRAME0)
        for reg in range(0x00, 0x12):  # 18 bytes of LED enable bits.
            self._write_reg(reg, 0xFF)

        # Clear PWM registers.
        self.clear()
        self.show()

    def clear(self):
        for i in range(PWM_COUNT):
            self.buf[i] = 0

    def _pixel_addr(self, x, y):
        # Pico Scroll Pack uses a split/mirrored map on top of IS31's 9x16 grid.
        # This maps logical 17x7 (x,y) into IS31 PWM register index [0..143].
        if x > 8:
            x = x - 8
            y = (6 - y) + 8
        else:
            x = 8 - x
        return x * 16 + y

    def set_pixel(self, x, y, v):
        if x < 0 or x >= WIDTH or y < 0 or y >= HEIGHT:
            return
        if v < 0:
            v = 0
        elif v > 255:
            v = 255
        self.buf[self._pixel_addr(x, y)] = v

    def fill_rect(self, x0, y0, w, h, v):
        for yy in range(y0, y0 + h):
            for xx in range(x0, x0 + w):
                self.set_pixel(xx, yy, v)

    def show(self):
        self._select_bank(self._BANK_FRAME0)
        # PWM registers start at 0x24, 1 byte per LED.
        self.i2c.writeto_mem(self.addr, 0x24, self.buf)


GLYPHS_3x5 = {
    "0": (
        "111",
        "101",
        "101",
        "101",
        "111",
    ),
    "1": (
        "010",
        "110",
        "010",
        "010",
        "111",
    ),
    "2": (
        "111",
        "001",
        "111",
        "100",
        "111",
    ),
    "3": (
        "111",
        "001",
        "111",
        "001",
        "111",
    ),
    "4": (
        "101",
        "101",
        "111",
        "001",
        "001",
    ),
    "5": (
        "111",
        "100",
        "111",
        "001",
        "111",
    ),
    "6": (
        "111",
        "100",
        "111",
        "101",
        "111",
    ),
    "7": (
        "111",
        "001",
        "010",
        "010",
        "010",
    ),
    "8": (
        "111",
        "101",
        "111",
        "101",
        "111",
    ),
    "9": (
        "111",
        "101",
        "111",
        "001",
        "111",
    ),
    "N": (
        "101",
        "111",
        "111",
        "111",
        "101",
    ),
    "R": (
        "110",
        "101",
        "110",
        "101",
        "101",
    ),
}


def draw_glyph_3x5(display, x, y, ch, val):
    glyph = GLYPHS_3x5.get(ch)
    if not glyph:
        return
    for yy, row in enumerate(glyph):
        for xx, bit in enumerate(row):
            if bit == "1":
                px = x + (2 - xx if TEXT_FLIP_X else xx)
                py = y + (4 - yy if TEXT_FLIP_Y else yy)
                display.set_pixel(px, py, val)


def draw_text_3x5(display, x, y, text, val, spacing=1):
    cx = x
    for ch in text:
        draw_glyph_3x5(display, cx, y, ch, val)
        cx += 3 + spacing


def clamp(v, lo, hi):
    if v < lo:
        return lo
    if v > hi:
        return hi
    return v


def parse_telem_line(line):
    # T,speed,rpm,rpm_max,gear,brake,accel,rumble,abs,tc,surface,shift
    parts = line.strip().split(",")
    if len(parts) != 12 or parts[0] != "T":
        return None
    try:
        return {
            "speed": int(parts[1]),
            "rpm": int(parts[2]),
            "rpm_max": max(1, int(parts[3])),
            "gear": int(parts[4]),
            "brake": int(parts[5]),
            "accel": int(parts[6]),
            "rumble": int(parts[7]),
            "abs": int(parts[8]),
            "tc": int(parts[9]),
            "surface": int(parts[10]),
            "shift": int(parts[11]),
        }
    except ValueError:
        return None


def send_button_event(name, down):
    state = "DOWN" if down else "UP"
    sys.stdout.write("BTN,%s,%s\n" % (name, state))


def render_race_page(display, telem, base):
    display.clear()

    speed_kph = clamp(telem["speed"], 0, MAX_SPEED_KPH)
    speed_mph = clamp(int(speed_kph * KPH_TO_MPH + 0.5), 0, MAX_SPEED_MPH)
    rpm = max(0, telem["rpm"])
    rpm_max = max(1, telem["rpm_max"])
    gear = telem["gear"]
    brake = clamp(telem["brake"], 0, 100)
    accel = clamp(telem["accel"], 0, 100)
    shift = clamp(telem["shift"], 0, 255)
    top_base = max(1, base // 3)
    bottom_base = max(1, base // 4)

    # Layout for 17x7:
    # - Row 0: RPM bar across full width
    # - Left: large gear glyph
    # - Center: speed as 3x5 digits
    # - Last two columns: vertical brake/accel bars
    # - Row 6: shift flash or rumble stripe

    # Row 0: RPM bar across full width.
    rpm_cols = (rpm * WIDTH) // rpm_max
    for x in range(WIDTH):
        v = top_base if x < rpm_cols else 0
        # Last columns brighten near shift.
        if x >= WIDTH - 3 and shift > 180 and x < rpm_cols:
            v = min(255, top_base + 25)
        display.set_pixel(x, 0, v)

    # Gear marker in first column:
    # gear 1 -> (0,1), gear 2 -> (0,2), etc.
    if gear <= 0:
        gear_y = 1          # neutral shown at top of gear lane
    elif gear == 10:
        gear_y = 6          # reverse at bottom
    else:
        gear_y = clamp(gear, 1, 6)
    display.set_pixel(0, gear_y, min(255, base + 70))

    # Center speed text (integer km/h).
    speed_txt = str(speed_mph)
    txt_w = len(speed_txt) * 3 + (len(speed_txt) - 1) * 1
    sx = (WIDTH - txt_w) // 2
    draw_text_3x5(display, sx, 1, speed_txt, min(255, base + 25), spacing=1)

    # Last two columns as vertical bars (rows 1..5).
    brake_h = (brake * 5) // 100
    accel_h = (accel * 5) // 100
    for i in range(5):
        y = 5 - i
        if i < brake_h:
            display.set_pixel(WIDTH - 2, y, min(255, base + 30))
        if i < accel_h:
            display.set_pixel(WIDTH - 1, y, min(255, base + 30))

    # Row 6: shift warning / rumble activity.
    if shift > 220:
        # Full-width shift flash.
        on = (time.ticks_ms() // 120) % 2
        if on:
            display.fill_rect(1, 6, WIDTH - 1, 1, min(255, bottom_base + 30))
    else:
        # Rumble activity stripe + heartbeat pixel at far right.
        rumble_cols = (clamp(telem["rumble"], 0, 255) * WIDTH) // 255
        for x in range(1, rumble_cols):
            display.set_pixel(x, 6, bottom_base)
        if (time.ticks_ms() // 400) % 2:
            display.set_pixel(WIDTH - 1, 6, bottom_base + 6)

    display.show()


def render_menu_page(display, menu_idx, level, base):
    display.clear()

    # Top marker row.
    for x in range(WIDTH):
        display.set_pixel(x, 0, base // 3)

    # Highlight selected "item" using one bright row.
    row = 1 + (menu_idx % 5)
    for x in range(WIDTH):
        display.set_pixel(x, row, min(255, base + 50))

    # Value bar on bottom row.
    level = clamp(level, 0, 100)
    n = (level * WIDTH) // 100
    for x in range(n):
        display.set_pixel(x, 6, min(255, base + 35))

    display.show()


def render_rev_only_page(display, telem, base):
    display.clear()

    rpm = max(0, telem["rpm"])
    rpm_max = max(1, telem["rpm_max"])
    shift = clamp(telem["shift"], 0, 255)

    cols_on = (rpm * WIDTH) // rpm_max
    ramp_base = min(255, base + 25)

    # Rising ramp from left (low) to right (high).
    # For lit columns, fill from bottom up to the ramp edge.
    for x in range(WIDTH):
        draw_x = (WIDTH - 1) - x
        y_edge = 6 - (x * 6) // (WIDTH - 1)
        if x < cols_on:
            for y in range(6, y_edge - 1, -1):
                display.set_pixel(draw_x, y, ramp_base)
            # Brighter edge helps readability.
            display.set_pixel(draw_x, y_edge, min(255, ramp_base + 20))
        else:
            # Dim unlit ramp outline.
            display.set_pixel(draw_x, y_edge, max(1, base // 6))

    # Shift warning in rev-only mode: top-row flash.
    if shift > 220 and ((time.ticks_ms() // 120) % 2):
        for x in range(WIDTH):
            display.set_pixel(x, 0, min(255, base + 35))

    display.show()


def main():
    # Auto-probe a few common Pico I2C pin mappings so bring-up is easier.
    i2c_candidates = [
        (I2C_ID, I2C_SDA_PIN, I2C_SCL_PIN),  # Pico Scroll Pack default.
        (1, 2, 3),
        (1, 6, 7),
        (0, 8, 9),
    ]

    i2c = None
    display = None
    scan_log = []

    for bus_id, sda_pin, scl_pin in i2c_candidates:
        try:
            probe = machine.I2C(
                bus_id,
                scl=machine.Pin(scl_pin),
                sda=machine.Pin(sda_pin),
                freq=I2C_FREQ,
            )
            devices = probe.scan()
            scan_log.append((bus_id, sda_pin, scl_pin, devices))
            if IS31_ADDR in devices:
                i2c = probe
                break
        except Exception as e:
            scan_log.append((bus_id, sda_pin, scl_pin, "ERR:%s" % e))

    if i2c is None:
        print("PICO_SCROLL_ERR: IS31FL3731 not found (addr=0x%02X)" % IS31_ADDR)
        print("I2C scan results:")
        for bus_id, sda_pin, scl_pin, devices in scan_log:
            print("  bus=%d sda=%d scl=%d -> %s" % (bus_id, sda_pin, scl_pin, devices))
        print("Check: Scroll Pack seated correctly, headers soldered, and pin mapping.")
        while True:
            time.sleep_ms(1000)

    try:
        display = IS31FL3731(i2c, IS31_ADDR)
    except Exception as e:
        print("PICO_SCROLL_ERR: display init failed: %s" % e)
        print("I2C scan results:")
        for bus_id, sda_pin, scl_pin, devices in scan_log:
            print("  bus=%d sda=%d scl=%d -> %s" % (bus_id, sda_pin, scl_pin, devices))
        while True:
            time.sleep_ms(1000)

    buttons = {}
    btn_state = {}
    for name, pin in BTN_PINS.items():
        buttons[name] = machine.Pin(pin, machine.Pin.IN, machine.Pin.PULL_UP)
        btn_state[name] = 1

    poll = uselect.poll()
    poll.register(sys.stdin, uselect.POLLIN)

    telem = {
        "speed": 0,
        "rpm": 0,
        "rpm_max": 9000,
        "gear": 0,
        "brake": 0,
        "accel": 0,
        "rumble": 0,
        "abs": 0,
        "tc": 0,
        "surface": 0,
        "shift": 0,
    }
    last_telem_ms = time.ticks_ms()

    page_menu = False
    rev_only = False
    menu_idx = 0
    menu_level = DEFAULT_BRIGHTNESS
    brightness = DEFAULT_BRIGHTNESS

    print("PICO_SCROLL_READY")

    while True:
        # Non-blocking serial receive.
        if poll.poll(0):
            line = sys.stdin.readline()
            if line:
                pkt = parse_telem_line(line)
                if pkt is not None:
                    telem = pkt
                    last_telem_ms = time.ticks_ms()

        # Button edge detection + local page/menu behavior.
        for name, pin in buttons.items():
            raw = pin.value()  # pull-up, so 0 = pressed
            prev = btn_state[name]
            if raw != prev:
                btn_state[name] = raw
                pressed = (raw == 0)
                send_button_event(name, pressed)

                if pressed:
                    if name == "A":
                        page_menu = not page_menu
                    elif name == "B":
                        # Toggle rev-only view when on race page.
                        if not page_menu:
                            rev_only = not rev_only
                        else:
                            menu_idx = (menu_idx + 1) % 5
                    elif page_menu and name == "X":
                        menu_level = clamp(menu_level - 5, 5, 100)
                        brightness = menu_level
                    elif page_menu and name == "Y":
                        menu_level = clamp(menu_level + 5, 5, 100)
                        brightness = menu_level

        # Timeout behavior: fade when telemetry is stale.
        age = time.ticks_diff(time.ticks_ms(), last_telem_ms)
        base = brightness if age < TELEM_TIMEOUT_MS else max(2, brightness // 4)
        base = clamp(base, 1, MAX_BRIGHTNESS)

        if page_menu:
            render_menu_page(display, menu_idx, menu_level, base)
        elif rev_only:
            render_rev_only_page(display, telem, base)
        else:
            render_race_page(display, telem, base)

        time.sleep_ms(FRAME_MS)


main()
