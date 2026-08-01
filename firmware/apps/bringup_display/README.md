# bringup_display — ILI9341 panel bring-up

Proves the 2.8" ILI9341 panel, its wiring and its reset timing, and
measures the strip-flush rate the radar will actually get. Patterns are
drawn through `radar_render`'s primitives into the same 240×20 strip
buffers the real UI uses (`docs/06`), so the FPS figure is the real path,
not a synthetic one.

## Build & flash

```sh
. ~/esp-idf/export.sh
idf.py -C firmware/apps/bringup_display set-target esp32s3 build
idf.py -C firmware/apps/bringup_display -p /dev/ttyUSB0 flash monitor
```

This app pulls the one managed dependency in the firmware,
`espressif/esp_lcd_ili9341`, which the component manager fetches on first
build (needs network access to the ESP component registry).

## Behaviour

Four patterns cycle automatically every 3 s:

1. Solid **red** full-screen with a white `RED` label — the colour
   byte-order check.
2. R/G/B colour bars plus an 8×8 font sample line.
3. 16-strip stress: full-screen redraws at max rate, printing achieved
   FPS once a second.
4. A moving white box on black — the tearing eyeball check.

`bl <0-100>` sets backlight brightness at any time.

## Acceptance — hardware checklist (project owner)

- [ ] Pattern 1 is **RED**, not cyan or blue, with a readable label.
      Cyan means the panel's colour element order is inverted — flip
      `rgb_ele_order` in `ili9341_disp.c`.
- [ ] Colour bars run R-G-B left→right and the text is crisp.
- [ ] Stress prints **FPS ≥ 5** (expect roughly 15–20 at 40 MHz).
- [ ] No white-screen boots across 5 power cycles — that would point at
      reset timing.
- [ ] `bl 25` visibly dims the panel, proving the night-mode plumbing.
