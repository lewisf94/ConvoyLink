# bringup_gps — NEO-6M fix + NMEA monitor

Proves the GY-NEO6MV2 module, its wiring and the patch antenna, and shows
what the `nmea` parser is actually making of the sentences.

Single unit — no second board needed.

## Build & flash

```sh
. ~/esp-idf/export.sh
idf.py -C firmware/apps/bringup_gps set-target esp32s3 build
idf.py -C firmware/apps/bringup_gps -p /dev/ttyUSB0 flash monitor
```

Console is the IDF `esp_console` REPL on UART0 at 115200. The GPS itself
is on **UART1** at 9600 8N1 (`docs/02` pin table: GPIO 38 in, 39 out).

## Commands

| Command | Action |
|---|---|
| `raw on\|off` | Echo raw `$G…` NMEA sentences as they arrive |
| `fix` | Print the current fix once |

A status line also prints automatically at 1 Hz:

```
fix=Y sats=9 hdop=1.2 lat=51.5074123 lon=-0.1278456 spd=0.0m/s crs=--- age=0.3s ok=142 bad=0
```

`crs=---` means the module reported no course (stationary). `age` is the
time since the last RMC with status `A`, so it keeps climbing if the fix
is lost while sentences still flow — `--` until the first ever fix.
`ok`/`bad` count recognised sentences and checksum failures.

Coordinates are formatted straight from the parser's `int32_t` e7 fields;
no float touches a coordinate anywhere in this app or the parser
(`docs/05`).

## Acceptance — hardware checklist (project owner)

- [ ] `raw on` shows `$G…` sentences within 2 s of boot. Silence means
      wiring (swapped TX/RX is the usual culprit); garbage characters mean
      a baud or logic-level problem.
- [ ] At a window or outdoors, a fix is acquired. **A cold start can take
      several minutes** — leave it a full 5 min before suspecting the
      hardware, and note that indoors it will usually never fix
      (`docs/05`).
- [ ] `lat`/`lon` match your phone's position to ~4 decimal places.
- [ ] `bad` stays ≈ 0 over 10 minutes. A rising count means wiring noise.
- [ ] Optional: power-cycle on the bench supply and note the warm-start
      time-to-fix in `tasks/STATUS.md`.
