# 05 — GPS, Geodesy & the Neighbour Table

## GPS module: GY-NEO6MV2 (u-blox NEO-6M)

- UART 9600 baud 8N1, NMEA 0183 output, 1 Hz. **Keep factory defaults** —
  no UBX configuration in v1 (GPIO 17 wired for the future).
- Cold start: 30 s – few minutes with sky view; warm start typically < 10 s.
  Indoors it will usually never fix — bring-up tests happen at a window/outside.
- We consume two sentence types and ignore everything else:
  - **RMC** — validity, lat, lon, speed (knots), course, date/time
  - **GGA** — fix quality, satellite count, HDOP, altitude
- Accept any talker ID (`$GPRMC`, `$GNRMC`, …) — match on the last 3 letters.

## NMEA parser (`components/nmea`, pure C)

Byte-feed API (no line assembly needed by callers):

```c
nmea_parser_t p; nmea_init(&p);
nmea_event_t ev = nmea_feed(&p, byte);   // NMEA_EV_NONE / _RMC / _GGA / _BAD
const nmea_fix_t *fix = nmea_fix(&p);    // merged latest state
```

`nmea_fix_t` (all integer fields — no floats in the parser):

| Field | Type | Encoding |
|---|---|---|
| `valid` | bool | RMC status == 'A' |
| `lat_e7`, `lon_e7` | int32_t | degrees × 1e7, signed (S/W negative) |
| `speed_dm_s` | uint16_t | 0.1 m/s (from knots × 0.514444) |
| `course_cdeg` | uint16_t | 0.01°, 0xFFFF when absent |
| `fix_quality` | uint8_t | GGA field (0/1/2…) mapped to 0/2/3 per `docs/03` |
| `sats` | uint8_t | GGA |
| `hdop_x10` | uint16_t | GGA, ×10 |
| `utc_hms` | uint32_t | hhmmss as decimal (logging only) |

Parsing rules the tests enforce:

1. Verify `*XX` checksum; on mismatch return `NMEA_EV_BAD`, never partial data.
2. `ddmm.mmmmm` → e7 using **int64 intermediate** math:
   `deg*10^7 + (mm_e5 * 10^2) / 60` — no floating point, no precision loss.
3. Empty fields (no-fix RMC has empty lat/lon) must not crash or emit junk.
4. Arbitrary garbage / partial sentences between valid ones must be skipped.
5. Max sentence length 82 chars; longer input resets the state machine.

## Geodesy (`components/convoy_geo`, pure C)

Convoy scale (< ~50 km separations) → **equirectangular approximation**,
single-precision float, Earth radius R = 6 371 000 m.

```c
// Relative position of B as seen from A, metres, +x = East, +y = North.
void geo_rel_xy(int32_t lat_a_e7, int32_t lon_a_e7,
                int32_t lat_b_e7, int32_t lon_b_e7,
                float *dx_m, float *dy_m);
float geo_dist_m(...);       // hypot of the above
float geo_bearing_deg(...);  // atan2(dx,dy) → [0,360), 0 = North
void  geo_fmt_dist(float m, char out[8]);  // "85m" "850m" "2.3km" "12km"
```

**Precision trap (tests cover it):** `float(9e8)` cannot represent 1e-7
degrees. Compute deltas in **int32 first** (`dlat = lat_b - lat_a`), then
convert the small delta to float:
`dy = dlat * 1e-7f * DEG2RAD * R;  dx = dlon * 1e-7f * DEG2RAD * R * cosf(lat_a_mid)`.

## Neighbour table (`components/neighbor_table`, pure C)

Fixed array of `CL_MAX_UNITS = 5` entries, keyed by `unit_id`. Owns all
"who's out there" logic; `radio_task` and `ui_task` only call its API.

```c
typedef enum { NT_LIVE, NT_STALE, NT_GHOST, NT_GONE } nt_tier_t;

nt_result_t nt_update_from_beacon(nt_t*, const cl_beacon_t*, uint32_t now_ms);
bool        nt_should_relay(nt_t*, const cl_beacon_t*);   // checks + marks (hop 0 only)
void        nt_note_relayed(nt_t*, uint8_t uid, uint16_t seq); // suppression on hearing hop-1
nt_tier_t   nt_tier(const nt_entry_t*, uint32_t now_ms);
int         nt_snapshot(const nt_t*, nt_entry_t out[CL_MAX_UNITS], uint32_t now_ms); // GONE excluded, sorted by distance? no — insertion order; UI sorts
```

Rules:

- Update only if `cl_seq_newer(beacon.seq, entry.last_seq)` — a relayed copy
  arriving after the original must not rewind state.
- `last_heard_ms` is the **local** clock at reception; all age math is
  wrap-safe: `(int32_t)(now - then)`.
- Relay bookkeeping per `docs/03`: `last_relayed_seq` per unit; `nt_should_relay`
  returns true at most once per `(uid, seq)` and only for `hop == 0`.

### Staleness tiers (single source of truth — UI renders these)

| Tier | Age since last beacon | Radar treatment |
|---|---|---|
| `NT_LIVE` | < 15 s | Solid coloured dot + initials |
| `NT_STALE` | 15 – 60 s | Hollow dot + initials + age text ("42s") |
| `NT_GHOST` | 60 s – 15 min | Dark-grey hollow dot + age ("7m") — "last seen here" |
| `NT_GONE` | > 15 min | Removed from radar and neighbour strip |

(The original brief said "expire after 15 s"; tiers supersede that because
the convoy explicitly expects out-of-range periods — a vanished dot is worse
than an aged one.)

## Own-position edge cases

- **No own fix**: relative geometry is impossible → radar area shows the
  NO FIX state (`docs/06`), neighbour strip still lists initials + age
  (distance "?"). Beacons still transmit with `fix_quality = 0`.
- **Neighbour without fix** (`fix_quality == 0`): listed in the strip as
  "online, no GPS", never plotted.
- **Own course**: valid when `speed_dm_s ≥ 15` (1.5 m/s); below that hold
  the last valid course for the compass arrow, hide after 60 s stationary.

## Beacon cadence

`gps_task` queues an own-beacon every `CL_BEACON_PERIOD_MS = 5000` (±200 ms
random jitter to avoid five units phase-locking), carrying the freshest fix
and its age. GPS fixes arrive at 1 Hz regardless; the radar's own-position
marker uses the live fix, not the beacon.
