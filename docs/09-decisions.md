# 09 — Decision Record (what was chosen, and what was ruled out)

This is the consolidated "why is it built this way" reference. `docs/00`
carries the dated decision *log* (v1 → v2 → v2.1 → v3, in the order the
calls were made); this doc is the *thematic* version — each major choice
with its rationale and the alternatives that were considered and rejected.

Where the two disagree, the newest dated entry in `docs/00` wins; this doc
is kept in sync with the current design (v3).

## Summary

| # | Area | Chosen | Main alternatives rejected |
|---|---|---|---|
| 1 | Product form | All-in-one dedicated device | Phone app; off-the-shelf handhelds |
| 2 | MCU | ESP32-S3-DevKitC-1 (N16R8) | Classic ESP32; integrated boards (T-Deck / T-TWR) |
| 3 | Toolchain | ESP-IDF v5.3 (native) | Arduino; PlatformIO |
| 4 | Position radio | LoRa (Semtech SX1262) | NRF24L01+; ESP-NOW; cellular |
| 5 | Position protocol | Custom 32-byte beacon + single-hop relay | Meshtastic as the product; full mesh routing |
| 6 | Voice architecture | Digital, transport-abstracted | Analog SA818 as default; no voice |
| 7 | Voice default transport | ESP-NOW (2.4 GHz) | SX1262/Codec2 first; SA818; separate radios |
| 8 | Voice range upgrade | SX1262 / Codec2 (experiment) | Making it the default before field data |
| 9 | Audio hardware | I²S: INMP441 mic + MAX98357A amp | Analog MAX9814 + PAM8403; ESP32 ADC/DAC |
| 10 | Display rendering | ILI9341 + 20-px strip rendering | Full 240×320 framebuffer |
| 11 | Legality stance | Stay inside a licence-exempt envelope | SA818-on-PMR446 w/ external antenna; type-approval |
| 12 | Range philosophy | Radar = km awareness; voice = shorter, honest | Promising multi-km voice |
| 13 | Dev method | Docs-as-law + task queue + host tests + sim | Ad-hoc coding; hardware-first |

---

## 1. Product form — a dedicated all-in-one device

**Chosen:** one self-contained, dash-mounted unit per car doing GPS radar
**and** push-to-talk voice, powered from the 12 V accessory socket.

**Why:** the entire point is off-grid group awareness with zero setup on
trip day — no phones, no signal, no pairing. A single box that powers on
with the ignition is the product.

**Ruled out:**
- *Phone app* — needs signal/data, batteries, and per-user setup; fails
  exactly where the trip goes (motorway, mountains, abroad).
- *Off-the-shelf handhelds only* — solves voice but gives no radar, which
  is the novel half. (Cheap PMR446 handhelds remain a fine *complement* for
  anyone who wants extra voice range — see §11.)

## 2. MCU — ESP32-S3-DevKitC-1 (N16R8)

**Chosen:** classic-era ESP32 first, then moved to the **ESP32-S3** once
voice went digital.

**Why:** the S3 gives two I²S peripherals (clean digital audio), native
USB-JTAG/serial, more RAM (8 MB PSRAM), more usable GPIO, and a faster CPU.
ESP-IDF v5.3 targets it directly. The N16R8 is the common reference board.

**Ruled out:**
- *Classic ESP32-WROOM-32* — was the pick while voice was analog; its only
  advantage here was the built-in DAC, which digital I²S audio makes
  irrelevant. Retired to spares.
- *Integrated boards (LILYGO T-Deck, T-TWR Plus)* — attractive parts lists
  (screen/radio/GPS/battery on one board) but the wrong form factor (a
  handheld, not a dash unit), cramped/committed GPIO, and in the T-TWR's
  case an SA818 with no open firmware. We want a purpose-built dash box on
  known-good discrete parts.

## 3. Toolchain — ESP-IDF v5.3 (native)

**Chosen:** ESP-IDF with CMake and `idf.py`.

**Why:** first-class I²S/ESP-NOW/SPI drivers, reproducible headless builds
(so CI and a cheaper model can compile every target), and no library-manager
surprises.

**Ruled out:**
- *Arduino* — easiest ramp, but no reproducible CI and weaker driver
  control for the timing-sensitive radio/audio paths.
- *PlatformIO* — repo-based and CI-friendly, but still the Arduino
  framework underneath; ESP-IDF is the better fit once you're doing custom
  radio and DMA audio.

## 4. Position radio — LoRa (Semtech SX1262, EBYTE E22-900M22S)

**Chosen:** sub-GHz LoRa for the position beacons.

**Why:** this is the range decision. LoRa's chirp modulation plus sub-GHz
propagation reaches **2–5 km car-to-car** — the same class of hardware
Meshtastic proves at 1–5 miles. One module tunes 868 (EU) and 915 (US/AU).
Our 5-second beacons use ~1–6 % airtime, comfortably licence-free (§11).

**Ruled out:**
- *NRF24L01+ (the original plan)* — 2.4 GHz, ~300 m–1 km at best in
  vehicles, fixed channel, no per-packet signal metrics. Outclassed on
  range; retired.
- *ESP-NOW for position* — works, but 2.4 GHz gives ~150–400 m; fine for
  voice-range, too short as the awareness layer.
- *Cellular/GSM telemetry* — defeats the "no infrastructure" premise.

## 5. Position protocol — custom 32-byte beacon + single-hop relay

**Chosen:** a fixed 32-byte beacon (`convoy_proto`, `docs/03`) broadcast
every 5 s, with each car rebroadcasting others' beacons once (single hop,
with suppression).

**Why:** tiny, self-describing packets; worst-case airtime is known; the
single hop roughly doubles radar coverage along a strung-out convoy without
the complexity or latency of true mesh routing. Loss-tolerant by design
(dropped beacon = a slightly older dot, never a crash).

**Ruled out:**
- *Meshtastic as the product* — it's excellent LoRa position-sharing
  firmware and a great **range-validation tool**, but it's data-only (no
  voice), a heavy Arduino/C++ codebase awkward to embed, and adopting it
  wholesale would mean giving up the bespoke radar UI. We keep our lean
  protocol and use Meshtastic to sanity-check range.
- *Full mesh routing / RF24Network* — more hops than a convoy needs, at real
  complexity cost.

## 6. Voice architecture — digital, transport-abstracted

**Chosen:** one digital pipeline (I²S mic → codec → frames → jitter buffer
→ I²S amp) with a **swappable transport** behind a common interface
(`docs/04`).

**Why:** the audio pipeline is identical whatever radio carries the frames,
so we build it once and A/B transports in real cars instead of betting up
front. Digital also restores **talker identity** (frames carry the sender
id → the radar shows who's speaking) and per-frame codec state for loss
resilience.

**Ruled out:**
- *Analog SA818 as the default* — see §7 and §11. Kept only as a licensed
  variant.
- *No voice* — voice-in-the-box is a core requirement, not a stretch.

## 7. Voice default transport — ESP-NOW (2.4 GHz)

**Chosen:** ship **ESP-NOW** first (the S3's own radio), ADPCM codec.

**Why:** licence-free under the ≤100 mW EIRP 2.4 GHz wideband-data
exemption; **zero new RF and no new parts**; validates the whole audio chain
with no radio unknowns; and it's architecturally clean — voice on the Wi-Fi
radio, positions on the SX1262, two independent radios running at once.

**Ruled out (as the *default*):**
- *SX1262 / Codec2 first* — better range, but it shares the LoRa chip
  (mode-switch arbitration), needs an FSK driver + Codec2, and is an
  untested 5 mW link — too many unknowns to lead with. It's the planned
  **range upgrade** (§8), not the starting point.
- *Analog SA818* — see §11; not licence-free as we'd build it.
- *Separate handhelds* — considered for the "no licence" goal, but the user
  wants one integrated device (they said so explicitly). Handhelds remain a
  fine optional complement, not the built-in path.

## 8. Voice range upgrade — SX1262 / Codec2 (kept as an experiment)

**Chosen:** implement a **second** transport — Codec2-3200 over SX1262 GFSK
at 869.7–870 MHz / ≤5 mW ERP (no duty limit on that sub-band) — as task T22,
then A/B it against ESP-NOW in the field. The winner becomes the default.

**Why:** ~9 dB of sub-GHz propagation advantage should give roughly 2.5–3×
the ESP-NOW range (~0.5–1.5 km). Because the pipeline is transport-abstract,
this is "add a transport," not "rewrite voice."

**Ruled out:** making it the default before real range data exists.

## 9. Audio hardware — I²S (INMP441 mic + MAX98357A amp)

**Chosen:** digital I²S mic and I²S class-D amp, sharing one full-duplex
I²S peripheral on the S3.

**Why:** on the S3 this is simpler and cleaner than analog — no pre-amp, no
DAC, no ground-loop hum, no AGC tuning; digital in, class-D out; the amp
drives the 4 Ω speaker directly.

**Ruled out:**
- *MAX9814 + PAM8403 (owned analog parts)* — needed by the analog-SA818
  plan; obsolete once voice is digital. To spares.
- *ESP32 built-in ADC/DAC* — the classic-ESP32 route; on that chip the
  ADC/DAC-over-I²S0 path is half-duplex and fiddly (it was v1's biggest
  risk). The S3 has no DAC anyway. I²S codecs sidestep all of it.

## 10. Display rendering — ILI9341 with 20-px strip rendering

**Chosen:** render the radar in 240×20 horizontal strips (double-buffered)
rather than a full framebuffer.

**Why:** keeps RAM tiny and the pure-C `radar_render` contract identical
between firmware and the desktop simulator (same code, same pixels). A full
framebuffer would now *fit* on the S3, but strips keep the sim/firmware
parity and the allocation-free discipline.

**Ruled out:** full 240×320 framebuffer (150 KB) — unnecessary, and it would
fork the render path from the simulator.

## 11. Legality stance — operate inside a licence-exempt envelope

**Chosen:** every transmitter runs within the technical limits of a
licence-exempt allocation — LoRa on 868/915 (§4), voice on ESP-NOW
(≤100 mW EIRP) or 5 mW sub-GHz (§7–8). No individual licence needed by
anyone.

**Why (and an important correction):** an earlier draft called an
**SA818 on PMR446 with an external antenna** a "grey area." That was wrong.
PMR446's licence exemption is conditional on the *equipment class* —
hand-portable, **integral non-removable antenna**, ≤500 mW ERP, conformity
to **EN 303 405**. An SA818 with a whip fails those conditions by
construction, so it would be *non-compliant* licence-exempt operation, not a
grey area. The lawful licence-free route is to keep the emission **inside an
exemption's envelope** — which is exactly what the LoRa link and the two
digital voice transports do.

**Ruled out:**
- *SA818-on-PMR446 with external antenna as the default* — not licence-free
  as built (above). Parked as a **licensed variant**: on 70 cm at 1 W it is
  fully legal *if every talker holds an amateur Foundation licence*, and it
  outranges the licence-free options (1–5 km) — the right answer for a
  licensed group, the wrong default for a friends' build.
- *Building a fully type-approved integral-antenna PMR446 unit* —
  theoretically licence-free, but needs EN 303 405 lab testing;
  disproportionate for a small hobby run.

Residual honesty: *any* self-built transmitter carries some conformity
formality (EN 300 328 for 2.4 GHz, EN 300 220 for 869 MHz apply to the LoRa
side too). The line that matters in practice is *parameters inside the
exemption envelope* vs *outside it*.

## 12. Range philosophy — radar is the awareness layer; voice is honest

**Chosen (design principle):** the LoRa radar is the km-scale "where is
everyone" tool; voice is the shorter-range "we're roughly together" channel.
Cars that drop out of beacon range **age into ghost dots** with a
"last seen" time rather than vanishing.

**Why:** the honest reality is that both licence-free voice options give up
multi-km range — but that's fine, because that's when you actually talk. The
radar keeps working far past voice range and never loses a car silently, so
you always know where to regroup.

**Ruled out:** promising multi-km voice on licence-free hardware. It doesn't
exist without either a licence (§11) or accepting the radar as the real
long-range layer.

## 13. Development method — docs-as-law + task queue + host tests + sim

**Chosen:** design docs are binding; implementation is a queue of
self-contained tasks (`tasks/`) with exact interface contracts and
acceptance tests; pure-C logic is host-tested under ASan/UBSan; a desktop
simulator runs the real render/geo code with no hardware; CI builds every
firmware target.

**Why:** it lets the expensive design thinking happen once, up front, then
lets a **cheaper model execute the bulk** reliably — every task says exactly
what to build and how it's verified, and CI/host-tests catch regressions
mechanically. It also means most of the product can be built and *seen*
(via the simulator) before any hardware exists.

**Ruled out:** ad-hoc coding, or a hardware-first approach where integration
bugs on unverified wiring cost the most to find.
