# Racepad Integration Contract

This document defines the integration between `racecontroller` telemetry and `racepad` (OpenFFBoard + nRF bridge).

## Scope

- Telemetry source stays in `racecontroller` (`sim`, Dirt Rally, or other UDP feed).
- `racepad_bridge` converts telemetry into FFBoard serial commands over USB CDC.
- No firmware protocol changes are required for this first integration path.

## Confirmed FFBoard Command Surface

The following commands are implemented in the racepad firmware command handlers and are used by the bridge.

### Class Prefixes

- `axis` from `Axis` (`CommandHandler("axis", ...)`)
- `fx` from `EffectsCalculator` (`CommandHandler("fx", ...)`)
- `sys` from `SystemCommands` (`CommandHandler("sys", ...)`)

### Axis Commands (per axis instance)

Used with explicit axis instance, e.g. `axis.0.power=9000;`

- `power` (overall force strength)
- `idlespring` (idle centering spring)
- `axisdamper` (always-on damper)
- `axisfriction` (always-on friction)
- `axisinertia` (always-on inertia)

### Effect Calculator Commands (global)

Used as `fx.spring=64;`, `fx.damper=80;`, etc.

- `spring`
- `damper`
- `friction`
- `inertia`

### Command Format

FFBoard parser accepts:

- `cls.cmd=value;`
- `cls.instance.cmd=value;`
- Optional `?` and `!` variants for read/info.

Commands may be newline terminated; parser normalizes newline to `;`.

## Telemetry Input Contract

Bridge input is `telem_packet_t` from `include/telemetry_proto.h` on UDP (default `5100`).

Required fields for mapping:

- `speed_kph`, `rpm`, `brake_pct`, `accel_pct`
- `rumble`, `abs_active`, `tc_active`, `surface`, `shift_light`

Stale telemetry timeout: `500 ms` (no updates sent after timeout).

## Bridge Output Mapping (v1)

`src/racepad_bridge.c` maps telemetry -> command intents:

- `axis.N.power`: base force envelope (accel/brake + ABS/TC bias)
- `axis.N.idlespring`: stronger at lower speed
- `axis.N.axisdamper`: rises with braking and speed
- `axis.N.axisfriction`: rises with rumble/surface activity
- `axis.N.axisinertia`: rises with ABS/TC and braking
- `fx.spring|damper|friction|inertia`: global effect tuning from shift/vehicle dynamics

## Rate Limiting and Queue Protection

Bridge applies:

- Fixed send loop cap (`--hz`, default `20 Hz`)
- Delta suppression:
  - `power`: send only if change >= `200`
  - `% channels`: send only if change >= `3`

This keeps command traffic bounded for CDC + ESB transport.

## Bring-up Checklist

1. Build:
   - `make sim`
   - `make bridge`
2. Run telemetry source:
   - `./telemetry_sim -v`
3. Run bridge:
   - `./racepad_bridge --serial /dev/tty.usbmodemXXXX --axis 0 --hz 20`
   - Or dry run: `./racepad_bridge --dry-run -v`
4. Validate on racepad serial side that commands arrive and parse.
5. Verify no sustained queue saturation in nRF bridge logs.

## Decision: CDC Command Mode vs Native Packet

Current decision: **keep CDC command mode** for first integration.

Why:

- Uses existing, proven FFBoard command handlers.
- Zero firmware packet changes needed in nRF or STM32 paths.
- Faster to tune mapping and update rates with host-only iterations.

Future trigger for native telemetry packet:

- If command traffic or latency becomes limiting, define a dedicated packet type in nRF bridge and a firmware-side telemetry consumer module. Keep CDC mode as fallback.
