# 04 — Voice: Digital PTT, Transport-Abstracted (v3)

Voice is **digital, half-duplex push-to-talk**. One audio pipeline
(I²S mic → codec → frames → jitter buffer → I²S amp) runs the same whatever
radio carries the frames — the **transport is swappable**. We ship the
ESP-NOW transport first (licence-free, no new RF, already on the board) and
add the SX1262/Codec2 transport as a field-tested range upgrade.

This supersedes the v2 analog-SA818 plan (`docs/00` v3 decision log). The
SA818 survives only as the *licensed variant* appendix at the end of this
doc.

```
TX: INMP441 ─I²S─► audio_io ─► codec.encode ─► voice frame ─► transport.send
RX: transport.recv ─► jitter buffer ─► codec.decode ─► audio_io ─I²S─► MAX98357A ─► speaker
```

## Why digital, and why this order

- **Legality (the corrected story).** PMR446's licence exemption is
  conditional on the *equipment class* — hand-portable, **integral
  non-removable antenna**, ≤500 mW ERP, **EN 303 405** conformity — so an
  SA818 with a whip is non-compliant licence-exempt operation, not a "grey
  area." The lawful licence-free route is to keep the emission **inside an
  exemption's technical envelope**, exactly as our LoRa beacons already do.
  Two such envelopes fit an all-in-one build (table below).
- **The pipeline is transport-independent**, so we build it once and A/B the
  radios in real cars rather than betting up front.
- **ESP-NOW first** validates the whole audio chain with **zero RF unknowns
  and no new parts**, and is architecturally clean: voice on the 2.4 GHz
  radio, positions on the SX1262 — two independent radios, simultaneous.
- The S3 makes digital audio *easy* (unlike the classic ESP32 that made v1's
  audio the top risk): two I²S peripherals, DMA, no DAC/ADC contortions.

## Legality table (UK, licence-free unless noted)

| Transport | Band / limit | Exemption basis | Range | Status |
|---|---|---|---|---|
| **ESP-NOW** | 2.4 GHz, **≤100 mW EIRP** | Wideband data / 2.4 GHz SRD | ~150–400 m | **ships first** |
| **SX1262 Codec2/GFSK** | **869.7–870 MHz, 5 mW ERP**, no duty limit | Non-specific SRD (EN 300 220) | ~0.5–1.5 km | range-upgrade experiment |
| SA818 analog FM | 446 MHz PMR446 | **NOT met** with external antenna (EN 303 405 equipment class) | 1–5 km | ✗ not licence-free as built |
| SA818 analog FM | Ham 70 cm, 1 W | Amateur licence (each operator) | 1–5 km | licensed variant only |

Residual honesty: *any* self-built transmitter carries some conformity
formality (EN 300 328 for 2.4 GHz, EN 300 220 for 869 MHz apply to the
LoRa side too). The line that matters in practice is **emission parameters
inside the exemption envelope** (ESP-NOW / 5 mW SX1262 — defensible homebrew
SRD) **vs outside it** (SA818 on PMR446 — the equipment class itself fails).
A fully type-approved integral-antenna PMR446 build is *theoretically*
licence-free but needs EN 303 405 lab testing — disproportionate here.

## Audio pipeline

8 kHz mono speech, 20 ms frames (160 samples). All stages are
transport-agnostic.

### Capture / playback — `audio_io` (I²S, `docs/02` pins)

The S3 runs **one I²S peripheral in full-duplex**: shared BCLK + WS,
`SD_IN` from the INMP441, `SD_OUT` to the MAX98357A. Half-duplex PTT means
we only ever *use* one direction at a time, but full-duplex wiring keeps the
clocks shared and the driver simple.

- Capture: I²S RX, 32-bit slots (INMP441 is 24-bit left-justified in 32),
  take the top 16 bits → int16 PCM. Mic is wired L/R→GND (left slot).
- Playback: I²S TX, 16-bit, straight to the MAX98357A (it has its own
  class-D; no DAC involved). Idle: stop TX (amp `SD` auto-mutes).
- No allocation after init; DMA buffers sized to a small multiple of a
  20 ms frame.

### Codecs — `adpcm` (T02) and `codec2` (transport B)

State travels *per frame* so a lost packet costs only its own 20 ms.

| Codec | Used by | Rate | 20 ms frame |
|---|---|---|---|
| **IMA ADPCM** | ESP-NOW | 32 kbps | 80 B payload + 3 B seed |
| **Codec2 3200** | SX1262 GFSK | 3.2 kbps | 8 B payload (stateless per frame) |

ADPCM reuses the T02 component (un-superseded). Codec2 is vendored for the
follow-on transport only.

### Voice frame — `voice_proto.h` (defined in T18, this doc is its contract)

Frames are **not** the 32-byte LoRa beacon format — voice rides its own
transport, so it uses its own header (analogous to how `convoy_proto` owns
the beacon layout). Little-endian, packed:

```c
#define VF_MAGIC 0xC8               /* distinct from beacon CL_MAGIC 0xC7 */
enum { VF_CODEC_ADPCM = 1, VF_CODEC_CODEC2_3200 = 2 };
#define VF_F_START 0x01
#define VF_F_END   0x02

typedef struct __attribute__((packed)) {
    uint8_t  magic;      /* VF_MAGIC                                   */
    uint8_t  ver_codec;  /* (1<<4) | vf_codec                          */
    uint8_t  sender;     /* uid 0..4 — talker identity travels (docs/06)*/
    uint8_t  flags;      /* VF_F_START | VF_F_END                      */
    uint16_t seq;        /* per-sender, wraps; cl_seq_newer()          */
    uint8_t  n_bytes;    /* payload length                             */
    uint8_t  seed[3];    /* ADPCM predictor(2)+index(1); 0 for Codec2  */
    uint8_t  payload[];  /* codec bytes, n_bytes long                  */
} vf_hdr_t;              /* 9 B header; ESP-NOW frame ≈ 89 B, well under 250 */
```

### Jitter buffer & concealment (RX)

Ring of `CL_JITTER_SLOTS` frames ordered by `seq`. Prefill
`CL_JITTER_PREFILL` (3 frames ≈ 60 ms) before starting playback; then one
frame per 20 ms. Missing next `seq` → conceal (repeat previous frame at
half amplitude, up to 3×) then silence. Late frames dropped. **Talker
lock**: first sender of a burst owns it until END or 300 ms starvation;
other senders dropped meanwhile.

## Transport abstraction

```c
typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    esp_err_t (*deinit)(void);
    esp_err_t (*send)(const uint8_t *frame, size_t len);   /* one vf_hdr_t frame */
    int       (*recv)(uint8_t *frame, size_t max, uint32_t wait_ms);
    bool      (*busy)(void);   /* a burst from someone else is in progress */
} voice_transport_t;
```

`voice_task` holds one `const voice_transport_t *`, chosen at boot from NVS
(`voice_transport = espnow | sx1262`). Nothing else in the pipeline knows
which radio it is.

### Transport A — ESP-NOW (T18, ships first)

- `esp_wifi_init` + `esp_wifi_set_mode(STA)` + `esp_wifi_start`, **fixed
  channel `CL_ESPNOW_CHANNEL`**, then `esp_now_init` and a broadcast peer
  `FF:FF:FF:FF:FF:FF`. Optionally enable long-range PHY for a little more
  reach. **Never `esp_wifi_connect`, never start an AP, never scan** — this
  is the softened invariant (see below).
- `send` = `esp_now_send(broadcast, frame, len)`; `recv` drains an RX
  callback queue; `busy` = a frame seen in the last `CL_VOICE_HANGOVER_MS`.
- Independent of the SX1262 — positions keep flowing while you talk.

### Transport B — SX1262 / Codec2 GFSK (T19, range upgrade)

- 869.7–870 MHz, GFSK ~9.6 kbps, TX power set so **ERP ≤ 5 mW**
  (≈ 0–3 dBm at the chip after antenna gain — verify with the bring-up app).
- Codec2 3200; frames are tiny (8 B), so airtime is cheap.
- **Shared-radio arbitration is the catch**: the SX1262 can't do LoRa
  beacons and GFSK voice at once. `radio_task` owns the chip; this transport
  does **not** call the SX1262 directly — it requests a mode switch and
  queues frames through `radio_task`, which suspends LoRa-beacon RX for the
  duration of a voice burst and resumes in the gaps. This coupling is the
  entire reason B is a follow-on, not the default.

## PTT state machine (`voice_task` + `ctrl_task`)

```
IDLE ──PTT down, !busy──────────────► TX     (capture → encode → transport.send)
IDLE ──PTT down, busy──► ARMED_WAIT ──busy clears──► TX     (UI shows BUSY)
ARMED_WAIT ──PTT up──► IDLE
TX ──PTT up──► TX_DRAIN ──END frame sent──► IDLE
TX ──CL_VOICE_TX_MAX_MS cap──► TX_DRAIN     (stuck-button guard)
RX driven by transport.recv: first frame locks talker → play → END/starve → IDLE
```

"busy" = `transport.busy()`. First TX frame carries `VF_F_START`, last
`VF_F_END`. Half-duplex: while transmitting we don't play RX frames.

## Talker identity & UI

Digital frames carry `sender`, so **the radar shows who is talking again**
(v2's analog path couldn't). `voice_task` publishes
`voice_status ∈ {IDLE, TX, RX(uid), BUSY, FAULT}`; the UI renders the
talker's initials in the status bar and a border on their chip (`docs/06`).

## Constants (`convoy_cfg.h`, shared with tests)

| Constant | Value |
|---|---|
| `CL_AUDIO_RATE_HZ` | 8000 |
| `CL_VOICE_FRAME_MS` / `CL_VOICE_FRAME_SAMPLES` | 20 / 160 |
| `CL_JITTER_SLOTS` / `CL_JITTER_PREFILL` | 16 / 3 |
| `CL_VOICE_HANGOVER_MS` | 300 |
| `CL_VOICE_TX_MAX_MS` | 60000 |
| `CL_ESPNOW_CHANNEL` | 6 |
| `VF_MAGIC` | 0xC8 |

## The "no Wi-Fi" invariant, softened (v3)

v1/v2 said "no Wi-Fi/BT init ever." ESP-NOW needs Wi-Fi *initialised* but
**connectionless**. The invariant becomes: **Wi-Fi is brought up only for
ESP-NOW, in STA mode, on a fixed channel; the firmware never associates to
or hosts a network, never scans, and never enables Bluetooth.** Costs ~50 KB
RAM (fine on the S3). If `voice_transport = sx1262`, Wi-Fi stays down
entirely.

---

## Appendix — SA818 licensed variant (not the default build)

For a group where **every talker holds an amateur (Foundation) licence**,
an SA818S-U on 70 cm at 1 W is fully legal and outranges both licence-free
transports (1–5 km). It is kept as an *optional* build, not in the core BOM:

- Hardware: SA818S-U + UHF whip; its analog audio replaces the I²S chain
  (mic → SA818 MIC via 10 µF/47 kΩ; SA818 AF → the speaker amp). UART +
  PTT GPIO from the ESP32.
- Firmware: a third `voice_transport` (`sa818`) that keys PTT and RSSI-polls
  instead of moving frames — but note analog carries **no talker id**, so
  the UI falls back to a generic RX indicator for this variant only.
- Legality: legal on 70 cm simplex **only** with licences; do **not** run it
  on PMR446 with an external antenna (the whole reason v2 was corrected).
- This appendix is documentation only in v1.0; there is no numbered task for
  it (see `ROADMAP.md` stretch). Wiring/UART details are preserved in git
  history at the v2 `docs/04-voice-sa818.md` if needed.
