# 03 — Radio Link & ConvoyLink Protocol (CLP)

One NRF24L01+ carries everything. This doc is the wire-format contract:
`components/convoy_proto` implements exactly these structures, and the host
tests assert the sizes. **Changing anything here requires updating
convoy_proto, its tests, and this doc in the same commit.**

## Radio configuration (all units identical)

| Parameter | Value | Why |
|---|---|---|
| Channel | 76 (2.476 GHz) | Above Wi-Fi ch. 13 — clear of in-car phone hotspots |
| Data rate | 250 kbps | Best sensitivity (−94 dBm) → best range |
| TX power | 0 dBm reg. setting (max) + PA | PA/LNA module does the rest |
| CRC | 16-bit | |
| Auto-ACK / retries | **Disabled** (`EN_AA=0`, `SETUP_RETR=0`) | Everything is broadcast to 5 receivers; ACKs are meaningless |
| Payload | **Fixed 32 bytes**, no dynamic payloads | Simplicity + worst-case timing known |
| Address width | 5 bytes | |
| Address (TX and RX pipe 0) | `0x43 0x4C 0x4E 0x4B 0x31` ("CLNK1") | Shared broadcast address, all units |
| IRQ | RX_DR + TX_DS on GPIO 26 | No polling |

Every unit transmits and receives on the *same* address: NRF24 "broadcast".
There is no mesh routing layer (RF24Network is not used) — 5 nodes in mutual
range plus a single-hop beacon relay (below) covers the convoy case with far
less complexity.

### On-air timing (the number everything else derives from)

`preamble 8 + address 40 + PCF 9 + payload 256 + CRC 16 = 329 bits`
→ **1.32 ms per packet** at 250 kbps, plus 130 µs PLL settling per TX.
Call it **1.5 ms of channel time per packet**.

## Packet formats

All packets are exactly 32 bytes, little-endian, packed. Common 4-byte header:

```c
#define CL_MAGIC      0xC7
#define CL_PROTO_VER  1

typedef struct __attribute__((packed)) {
    uint8_t magic;     // CL_MAGIC
    uint8_t ver_type;  // (CL_PROTO_VER << 4) | cl_type
    uint8_t sender;    // unit_id 0..4
    uint8_t meta;      // per-type: beacon = hop count; voice = flags
} cl_hdr_t;

enum cl_type { CL_TYPE_BEACON = 1, CL_TYPE_VOICE = 2, CL_TYPE_PING = 3 };
```

### `CL_TYPE_BEACON` — position, every 5 s

```c
typedef struct __attribute__((packed)) {
    cl_hdr_t hdr;          // hdr.meta = hop: 0 = original, 1 = relayed
    uint16_t seq;          // per-sender, wraps
    char     initials[2];  // ASCII, e.g. "LF" — beacons are self-describing
    int32_t  lat_e7;       // degrees × 1e7 (±90°  → ±900,000,000)
    int32_t  lon_e7;       // degrees × 1e7 (±180° → ±1,800,000,000)
    uint16_t speed_dm_s;   // ground speed, 0.1 m/s units
    uint16_t course_cdeg;  // course over ground, 0.01°, 0..35999; 0xFFFF = invalid
    uint8_t  fix_quality;  // 0 = none, 2 = 2D, 3 = 3D
    uint8_t  sats;         // satellites used
    uint16_t fix_age_ms;   // age of the fix when sent (saturating)
    uint8_t  reserved[8];  // zero-filled
} cl_beacon_t;             // == 32 bytes (static_assert in convoy_proto.h)
```

A unit with no fix still beacons (`fix_quality = 0`) so others see it as
"online, no GPS" rather than absent.

### `CL_TYPE_VOICE` — one ADPCM audio frame

```c
typedef struct __attribute__((packed)) {
    cl_hdr_t hdr;          // hdr.meta flags: bit0 = BURST_START, bit1 = BURST_END
    uint16_t seq;          // per-sender, monotonic across bursts, wraps
    int16_t  predictor;    // IMA ADPCM decoder state at frame start
    uint8_t  step_index;   // IMA ADPCM step index at frame start (0..88)
    uint8_t  n_samples;    // 1..44 (44 except possibly the END frame)
    uint8_t  adpcm[22];    // 4-bit IMA samples, low nibble = earlier sample
} cl_voice_t;              // == 32 bytes
```

**Codec state travels in every frame.** A lost frame costs only its own
5.5 ms of audio; the decoder re-seeds from the next frame. This is the core
robustness decision for a no-ACK link — do not "optimise" it away.

44 samples @ 8 kHz = 5.5 ms per frame → **181.8 frames/s** while talking.

### `CL_TYPE_PING` — range testing only (bring-up app)

```c
typedef struct __attribute__((packed)) {
    cl_hdr_t hdr;          // meta unused (0)
    uint16_t seq;
    uint32_t uptime_ms;    // sender's clock, for RTT-style logging
    uint8_t  pattern[22];  // 0xA5 fill
} cl_ping_t;               // == 32 bytes
```

### Sequence-number comparison

`seq` wraps at 65536. Newer-than is signed-difference:
`bool cl_seq_newer(uint16_t a, uint16_t b) { return (int16_t)(a - b) > 0; }`
(provided by convoy_proto; use it everywhere, never `a > b`).

## Airtime budget (5 units)

| Traffic | Rate | Airtime |
|---|---|---|
| Voice (one talker, PTT held) | 181.8 pkt/s × 1.5 ms | **27 %** |
| Beacons (5 units / 5 s) | 1 pkt/s × 1.5 ms | 0.15 % |
| Beacon relays (worst case, 4 relays × 5 beacons / 5 s) | 4 pkt/s × 1.5 ms | 0.6 % |
| **Total worst case** | | **< 28 %** |

Ample headroom; collisions are possible (no carrier sense on NRF24) but
rare, and every packet type tolerates individual loss.

## Channel arbitration rules (implemented in `radio_task`, T16/T18)

1. `radio_task` is the only code touching the radio. It sits in RX
   (CE high) except while transmitting one queued packet (~1.5 ms).
2. **Voice wins.** While our PTT burst is active, relays are suppressed;
   own beacons still go out on schedule (they cost 1.5 ms).
3. **Courtesy lockout.** "Channel busy" = any voice frame received in the
   last 300 ms. Pressing PTT while busy arms the burst to start the moment
   the channel frees, and the UI shows BUSY (`docs/06`). No forced override
   in v1.
4. **Beacon politeness.** If busy when a beacon is due, defer it up to
   250 ms waiting for a 40 ms quiet gap, then send regardless.

## Beacon relay (single hop — the "several km" mitigation)

On receiving a beacon `B` with `hop == 0` from unit `u ≠ me`:

1. Update neighbour table (always, if `cl_seq_newer(B.seq, table[u].seq)`).
2. If `B.seq` not already relayed for `u` (per-unit `last_relayed_seq`):
   schedule a rebroadcast of `B` with `hop = 1`, `sender`/`seq`/payload
   unchanged, at `now + uniform(80..280 ms)`.
3. **Suppress**: cancel the scheduled relay if we hear any `hop == 1` copy
   of the same `(u, seq)` first — usually only one car relays each beacon.
4. Drop the scheduled relay if the channel is voice-busy when it fires.

Beacons with `hop == 1` are never relayed again (max 2 hops total including
origin). Expected effect: a middle car extends radar coverage roughly 2×
along the convoy line. Voice is never relayed.

## Loss behaviour summary

| Loss | Effect | Mitigation |
|---|---|---|
| One voice frame | 5.5 ms audio gap | Jitter buffer repeats last frame attenuated (`docs/04`) |
| One beacon | Dot position up to 10 s old | Staleness tiers (`docs/05`) |
| Sustained (out of range) | Dot goes stale → ghost | Relay + "last seen" UI, never silently vanishes |

## Driver notes (`components/nrf24`, T09)

Custom thin driver (no Arduino RF24 port): SPI at 8 MHz on HSPI-via-GPIO-
matrix pins (`docs/02`), IRQ-driven. Needed ops: register R/W, RX/TX FIFO
access, `FLUSH_TX/RX`, mode switches (CE/PRIM_RX), RPD read (crude signal
presence for the range app), `W_TX_PAYLOAD` (plain — ACKs are globally off).
TX sequence: CE low → write payload → PRIM_RX=0 → CE pulse ≥10 µs → await
TX_DS IRQ → back to RX. The datasheet register map is mirrored in the task
spec (T09); verify with `bringup_radio` before any integration work.

## Future transport upgrades (documented, not implemented)

The packet structs deliberately fit other links: SA818 (analog voice,
multi-km, licensing varies by country) would replace `CL_TYPE_VOICE` only;
LoRa SX1276 (multi-km telemetry at low rate) would replace `CL_TYPE_BEACON`
only. Keep `convoy_proto` transport-agnostic — nothing in it may include
NRF24 headers.
