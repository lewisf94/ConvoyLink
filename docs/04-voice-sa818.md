# 04 — Voice: SA818S-U Analog UHF (v2)

Voice is a **complete analog FM walkie-talkie on a module**. The ESP32
never touches audio samples: the MAX9814 mic amp feeds the SA818's mic
input, the SA818's audio output feeds the PAM8403 → speaker, and firmware
only (a) configures the channel over UART, (b) keys PTT, and (c) watches
for received carrier to drive the UI. This replaced the v1 digital-voice
plan (ADPCM over NRF24) — see `docs/00` decision log.

```
PTT button ─► ctrl_task ─► voice_task ─► SA818 PTT (GPIO, low = TX)
MAX9814 ──10µF+47k──► SA818 MIC_IN          SA818 AF_OUT ──10µF──► PAM8403 ─► speaker
ESP32 UART1 (9600) ◄──► SA818 RXD/TXD       (channel config + RSSI polling)
```

Wiring, decoupling, heatsink and level details: `docs/02` §SA818. The
gotchas that matter: never TX without the antenna; 220 µF + 100 nF + 10 nF
at VCC or your transmitted audio hums; heatsink the ground pad for long
transmissions; the module cuts audio below 300 Hz (fine for speech).

## UART control protocol (SA818 firmware, 9600 8N1)

Commands end `\r\n`; module replies `+DMO…:0` on success. The full set the
`sa818` component uses:

| Command | Purpose |
|---|---|
| `AT+DMOCONNECT` | handshake after power-up (retry ×3, 500 ms apart) |
| `AT+DMOSETGROUP=BW,TXFREQ,RXFREQ,TX_CTCSS,SQ,RX_CTCSS` | the whole channel in one line. BW: 0 = 12.5 kHz. Freq: `446.00625` style, TX = RX for simplex. CTCSS: code `0008` = 88.5 Hz (convoy default — keeps random traffic out of your speaker; it is **not** privacy). SQ: squelch 0–8, default 4 |
| `AT+DMOSETVOLUME=x` | 1–8, speaker line level (default 4) |
| `AT+SETFILTER=0,0,0` | pre/de-emphasis + highpass + lowpass all normal |
| `AT+RSSI?` | received signal strength — polled for carrier detect |

Config is applied at boot from NVS (`region` + `voice_channel` + power
jumper) and re-applied on any UART error (the module forgets settings on
power loss).

## Carrier detect (busy/RX indication without digital metadata)

Analog FM carries no sender ID, so the UI shows a **generic RX state**
(`docs/06`), not talker initials. Detection: `voice_task` polls `AT+RSSI?`
every `CL_VOICE_RSSI_POLL_MS` (150 ms) while idle; RSSI above threshold =
carrier present → `voice_status = RX`. Carrier gone → hold BUSY for
`CL_BUSY_HANGOVER_MS` (500 ms), then idle. While we transmit, polling
pauses (half-duplex).

If the chosen SA818 carrier board exposes a hardware squelch output pin,
T12 may use it instead of polling (better latency) — treat as an optional
enhancement discovered at bring-up, not a dependency.

## PTT state machine (much simpler than v1's)

```
IDLE ──PTT down, no carrier──────────────► TX     (SA818 PTT low)
IDLE ──PTT down, carrier present──► ARMED_WAIT ──carrier clears──► TX
ARMED_WAIT ──PTT up──► IDLE                        (UI shows BUSY)
TX ──PTT up──► IDLE                                (SA818 PTT high)
TX ──CL_TX_MAX_MS (60 s) cap──► IDLE               (stuck-button guard)
```

The courtesy lockout is advisory (you *can't* hear anyone while
transmitting anyway — half-duplex FM); ARMED_WAIT just stops two cars
talking over each other from the start of a press.

## Channel plans & legality (owner picks at provisioning; firmware treats it as config)

The SA818S-U tunes 400–480 MHz, which covers several very different legal
regimes. `unitcfg voice …` (docs/07) stores the plan; the H/L jumper sets
power to match.

| Region | Plan | Power jumper | Legal status (owner verifies locally) |
|---|---|---|---|
| UK / EU | **PMR446**: 446.00625–446.19375 MHz, 16 ch | **0.5 W (L)** | PMR446 is licence-free for handhelds with integral antennas; a module + external antenna is **technically outside type-approval** — common hobbyist practice, low power, but formally grey. The pragmatic default |
| UK / EU | Amateur 70 cm (e.g. 433 MHz simplex) | 1 W (H) | Fully legal **if every person who transmits holds an amateur licence** (UK Foundation: short course + exam) |
| US | **GMRS** 462 MHz simplex channels | 1 W (H) | One $35 GMRS licence, no exam, covers the licensee's whole family for 10 years — the cleanest legal story |
| AU / NZ | **UHF CB** 476.425–477.4125 MHz | 1 W (H) | Class-licensed (licence-free to users) |

Ship default: PMR446 ch 3 (446.05625 MHz), CTCSS 88.5 Hz, 0.5 W.
Also handy: any cheap PMR446/GMRS handheld set to the same channel+CTCSS
interoperates — passengers or a chase car can join the voice net for £15.

## What firmware owns (component `sa818` + `voice_task`, tasks T12/T13/T18)

- `sa818` component: UART init on `VHF_TXD/VHF_RXD`, command send/parse
  with timeouts, `sa818_set_channel(plan)`, `sa818_ptt(bool)`,
  `sa818_rssi()`, config re-apply on error. No dynamic allocation.
- `voice_task` (replaces v1's `audio_task`): PTT state machine, RSSI
  polling, publishes `voice_status ∈ {IDLE, TX, RX, BUSY}` to shared state
  for the UI. Nothing else — there is deliberately no audio DSP anywhere.

## Numbers in `convoy_cfg.h`

| Constant | Value |
|---|---|
| `CL_VOICE_CTCSS_DEFAULT` | 8 (88.5 Hz) |
| `CL_VOICE_SQUELCH_DEFAULT` | 4 |
| `CL_VOICE_RSSI_POLL_MS` | 150 |
| `CL_BUSY_HANGOVER_MS` | 500 |
| `CL_TX_MAX_MS` | 60000 |
