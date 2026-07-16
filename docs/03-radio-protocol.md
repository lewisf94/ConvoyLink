# 03 — LoRa Data Link & ConvoyLink Protocol (CLP) — v3

The **SX1262 LoRa radio carries position data** (beacons + range-test
pings) and, with the optional SX1262 voice transport, Codec2 voice frames
too (`docs/04-voice.md`). The default ESP-NOW voice transport does **not**
touch this radio. This doc is the wire-format contract for the beacon link:
`components/convoy_proto` implements exactly these structures and the host
tests assert the sizes. **Changing anything here requires updating
convoy_proto, its tests, and this doc in the same commit.**

## Radio configuration (all units identical; region from NVS)

| Parameter | Value | Why |
|---|---|---|
| Modem | LoRa (SX1262 via E22-900M22S) | Meshtastic-proven multi-km car-to-car physics |
| Frequency | region `EU`: **869.525 MHz** · region `US`/`AU`: **915.000 MHz** | EU value sits in the 869.40–869.65 sub-band: 500 mW ERP, **10 % duty** allowed licence-free (UK IR2030/1/19, CEPT 70-03); US/AU 902–928 ISM has no duty limit |
| SF / BW / CR | **SF7 / 125 kHz / 4:5** | 32-byte airtime ≈ 61 ms; range budget already huge at SF7 with +22 dBm — do not raise SF without redoing the duty math |
| Preamble | 8 symbols | |
| Sync word | 0x12 (private) | Keeps LoRaWAN/Meshtastic traffic out of our RX |
| TX power | +22 dBm | ≈160 mW + antenna ≪ 500 mW ERP limit |
| CRC | on (LoRa payload CRC) | corrupt frames dropped by the modem |
| Payload | **fixed 32 bytes** | worst-case timing known; `cl_validate` guards the rest |
| IRQ | DIO1 (RX-done / TX-done) | no polling |

There is no routing layer. Every packet is a broadcast heard by whoever is
in range, plus the single-hop beacon relay below.

### Airtime & duty budget (the compliance table)

One 32-byte packet at SF7/125k/4:5 ≈ **61 ms** on air.

| Traffic | Rate | Airtime share |
|---|---|---|
| Own beacons | 1 per 5 s | 1.22 % |
| Relays (worst case: relaying all 4 others every cycle) | 4 per 5 s | 4.9 % |
| **Worst-case total per unit** | | **≈ 6.1 %** |

Under the EU sub-band's 10 % duty limit with margin, and relay suppression
(below) keeps the typical case near 2 %. **Any change to beacon period,
payload size, SF or relay policy must be re-checked against this table.**

## Packet formats

All packets are exactly 32 bytes, little-endian, packed. Common 4-byte
header:

```c
#define CL_MAGIC      0xC7
#define CL_PROTO_VER  1

typedef struct __attribute__((packed)) {
    uint8_t magic;     // CL_MAGIC
    uint8_t ver_type;  // (CL_PROTO_VER << 4) | cl_type
    uint8_t sender;    // unit_id 0..4
    uint8_t meta;      // beacon: hop count; ping: unused
} cl_hdr_t;

enum cl_type { CL_TYPE_BEACON = 1, /* 2 permanently reserved; v3 voice uses
                                      voice_proto (0xC8), not this link */
               CL_TYPE_PING = 3 };
```

### `CL_TYPE_BEACON` — position, every 5 s

```c
typedef struct __attribute__((packed)) {
    cl_hdr_t hdr;          // hdr.meta = hop: 0 = original, 1 = relayed
    uint16_t seq;          // per-sender, wraps
    char     initials[2];  // ASCII, e.g. "LF" — beacons are self-describing
    int32_t  lat_e7;       // degrees × 1e7
    int32_t  lon_e7;       // degrees × 1e7
    uint16_t speed_dm_s;   // ground speed, 0.1 m/s units
    uint16_t course_cdeg;  // course over ground, 0.01°, 0..35999; 0xFFFF = invalid
    uint8_t  fix_quality;  // 0 = none, 2 = 2D, 3 = 3D
    uint8_t  sats;
    uint16_t fix_age_ms;   // age of the fix when sent (saturating)
    uint8_t  reserved[8];  // zero-filled
} cl_beacon_t;             // == 32 bytes (static_assert in convoy_proto.h)
```

A unit with no fix still beacons (`fix_quality = 0`) so others see it as
"online, no GPS" rather than absent.

### `CL_TYPE_PING` — range testing only (bring-up app)

```c
typedef struct __attribute__((packed)) {
    cl_hdr_t hdr;          // meta unused (0)
    uint16_t seq;
    uint32_t uptime_ms;
    uint8_t  pattern[22];  // 0xA5 fill
} cl_ping_t;               // == 32 bytes
```

The SX1262 reports **RSSI and SNR per received packet** — the bring-up
range app logs both (a real improvement over v1's NRF24 1-bit RPD).

### Sequence-number comparison

`seq` wraps at 65536. Newer-than is signed-difference:
`bool cl_seq_newer(uint16_t a, uint16_t b) { return (int16_t)(a - b) > 0; }`
(provided by convoy_proto; use it everywhere, never `a > b`).

## Channel access rules (implemented in `radio_task`, T16)

1. `radio_task` is the only code touching the SX1262. It sits in RX
   (continuous) except while transmitting one packet (~61 ms).
2. **Listen-before-talk, cheap version**: don't start a TX while a
   reception is in progress (DIO1 preamble/header detect or modem-busy);
   defer up to 200 ms in 20 ms steps, then send anyway.
3. With the **default ESP-NOW voice transport**, beacons and relays are the
   only LoRa traffic — no priority problem, and voice (on the 2.4 GHz radio)
   shares nothing with this chip.
4. With the **optional SX1262/Codec2 voice transport** (`docs/04`),
   `radio_task` also owns the voice frames: it switches the chip LoRa↔GFSK,
   and a voice burst (short, tiny Codec2 frames) takes priority — beacons
   defer until the burst ends, then resume. This is the shared-radio
   arbitration that makes that transport a follow-on, not the default.

## Beacon relay (single hop — the range multiplier)

On receiving a beacon `B` with `hop == 0` from unit `u ≠ me`:

1. Update neighbour table (always, if `cl_seq_newer(B.seq, table[u].seq)`).
2. If `B.seq` not already relayed for `u` (per-unit `last_relayed_seq`):
   schedule a rebroadcast of `B` with `hop = 1`, `sender`/`seq`/payload
   unchanged, at `now + uniform(150..450 ms)` (window ≫ 61 ms airtime).
3. **Suppress**: cancel the scheduled relay if we hear any `hop == 1` copy
   of the same `(u, seq)` first — usually only one car relays each beacon,
   which is what keeps the duty budget near 2 %.
4. Beacons with `hop == 1` are never relayed again (max 2 hops total).

Expected effect: a middle car roughly doubles radar coverage along the
convoy line (2–5 km direct becomes useful at 4–10 km strung out).

## Loss behaviour summary

| Loss | Effect | Mitigation |
|---|---|---|
| One beacon | Dot position up to 10 s old | Staleness tiers (`docs/05`) |
| Sustained (out of range) | Dot goes STALE → GHOST with age | Relay + "last seen" UI, never silently vanishes |

## Driver notes (`components/sx1262`, T09)

Vendor the MIT-licensed **nopnop2002/esp-idf-sx126x** driver as the
component's core (attribution kept), wrapped behind our thin API: init with
the table above (region-selected frequency), non-blocking RX with DIO1
event → queue, blocking `send(32B)`, per-packet RSSI/SNR out. The E22's
TXEN/RXEN RF-switch GPIOs are driven by the driver around each TX/RX
transition (`docs/02` pin map). Verify with `bringup_radio` before any
integration work.

## Regulatory quick reference (owner responsibility, summarised)

| Region (NVS) | Beacon frequency | Rules met by this design |
|---|---|---|
| `EU` (incl. UK) | 869.525 MHz | ≤ 500 mW ERP, ≤ 10 % duty (we use ≤ 6 %) — licence-free SRD |
| `US` | 915.0 MHz | 902–928 MHz ISM digital modulation — licence-free |
| `AU`/`NZ` | 915.0 MHz | 915–928 MHz LIPD class licence |

Voice-side legality (ESP-NOW ≤100 mW EIRP; SX1262 Codec2 at 869.7–870 MHz,
5 mW ERP) is covered in `docs/04-voice.md`.
