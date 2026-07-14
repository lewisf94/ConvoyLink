# firmware/

- **`components/`** — shared ESP-IDF components. Two kinds:
  - *Pure C* (`convoy_proto`, `convoy_geo`, `adpcm`, `nmea`,
    `neighbor_table`, `radar_render`, `voice_pipe`): no ESP-IDF headers,
    unit-tested on the host (`test/host/`), reused by the simulator.
    `convoy_proto` is the exemplar — copy its layout, style and test suite
    shape.
  - *Hardware* (`convoy_pins`, `nrf24`, `gps_uart`, `audio_io`,
    `ili9341_disp`, `unit_cfg`): ESP-IDF APIs allowed, verified by the
    bring-up apps.
- **`apps/`** — each subdirectory is a standalone ESP-IDF project (see
  `apps/README.md` for the exact template). `convoylink` is the product;
  `bringup_*` apps validate one hardware subsystem each.

Build any app:

```sh
. ~/esp-idf/export.sh
idf.py -C firmware/apps/<app> set-target esp32
idf.py -C firmware/apps/<app> build            # flash monitor
```

Build everything (what CI does): `./tools/ci_build_apps.sh`.
