# T09 — `nrf24` driver + `convoy_pins`

**Depends:** none · **Phase:** M3 (ESP-IDF)
**Required reading:** `docs/03-radio-protocol.md` §Radio configuration +
§Driver notes; `docs/02-hardware.md` §Pin map

## Goal

A thin, IRQ-driven NRF24L01+ driver — SPI register protocol, fixed 32-byte
no-ACK broadcast, single-owner API — plus the shared pin-map header every
app uses.

## Deliverables

- `firmware/components/convoy_pins/include/convoy_pins.h` — header-only
  component: `#define CONVOY_PIN_<NET> <gpio>` for **every** row of the
  docs/02 pin table, plus `CMakeLists.txt`
  (`idf_component_register(INCLUDE_DIRS "include")`).
- `firmware/components/nrf24/include/nrf24.h`
- `firmware/components/nrf24/nrf24.c`
- `firmware/components/nrf24/CMakeLists.txt` (REQUIRES driver)

## Interface contract

```c
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

typedef struct {
    spi_host_device_t host;      /* SPI2_HOST (HSPI) per docs/02          */
    int sck, mosi, miso, csn, ce, irq;
} nrf24_pins_t;

esp_err_t nrf24_init(const nrf24_pins_t *pins, uint8_t channel,
                     const uint8_t addr[5]);   /* ends in RX mode        */
esp_err_t nrf24_send(const uint8_t payload[32]); /* blocking <= ~3 ms;
                     returns to RX mode; ESP_ERR_TIMEOUT if TX_DS never fires */
bool      nrf24_receive(uint8_t out[32], uint32_t wait_ms); /* from the
                     driver's internal RX queue (depth 8, drop-oldest)   */
bool      nrf24_rpd(void);          /* received-power flag, range app    */
uint8_t   nrf24_read_reg(uint8_t reg);
void      nrf24_dump_regs(void);    /* ESP_LOGI table of key registers   */
```

Concurrency contract (document in the header): `nrf24_init` once from
startup; afterwards **only one task** may call send/receive (radio_task /
the bring-up app's main loop). ISR does no SPI — it releases a semaphore;
an internal driver task (prio 13, core 1) drains the RX FIFO into the
queue and clears STATUS flags, also draining on a 50 ms poll as
missed-IRQ insurance.

## Register map (from the nRF24L01+ datasheet — use these, do not invent)

Commands: `R_REGISTER 0x00|r` `W_REGISTER 0x20|r` `R_RX_PAYLOAD 0x61`
`W_TX_PAYLOAD 0xA0` `FLUSH_TX 0xE1` `FLUSH_RX 0xE2` `NOP 0xFF`.
Registers: CONFIG 0x00, EN_AA 0x01, EN_RXADDR 0x02, SETUP_AW 0x03,
SETUP_RETR 0x04, RF_CH 0x05, RF_SETUP 0x06, STATUS 0x07, OBSERVE_TX 0x08,
RPD 0x09, RX_ADDR_P0 0x0A, TX_ADDR 0x10, RX_PW_P0 0x11, FIFO_STATUS 0x17,
DYNPD 0x1C, FEATURE 0x1D.
CONFIG bits: 6 MASK_RX_DR, 5 MASK_TX_DS, 4 MASK_MAX_RT, 3 EN_CRC, 2 CRCO,
1 PWR_UP, 0 PRIM_RX. STATUS bits (write-1-clear): 6 RX_DR, 5 TX_DS,
4 MAX_RT. FIFO_STATUS bit 0 = RX_EMPTY. RF_SETUP: bit 5 RF_DR_LOW
(250 kbps with bit 3 RF_DR_HIGH = 0), bits 2:1 = 0b11 max power → value
**0x26** for our link. STATUS is also clocked out on every command's first
byte — use that instead of extra reads where convenient.

Init sequence: 100 ms power-on wait → PWR_UP + EN_CRC + CRCO, 5 ms →
EN_AA=0x00, EN_RXADDR=0x01 (pipe 0), SETUP_AW=0x03, SETUP_RETR=0x00,
RF_CH=`channel`, RF_SETUP=0x26, RX_ADDR_P0=TX_ADDR=`addr`, RX_PW_P0=32,
DYNPD=0, FEATURE=0 → FLUSH both, STATUS=0x70 → PRIM_RX=1, CE high.
**Verify by reading back RF_CH and RX_ADDR_P0**; mismatch →
`ESP_ERR_NOT_FOUND` + error log ("check 3.3 V rail B / wiring") — this is
the wiring smoke test the bring-up app relies on.
TX path: CE low → PRIM_RX=0 → W_TX_PAYLOAD → CE ≥10 µs pulse → wait TX_DS
(semaphore, 5 ms timeout) → STATUS=0x70 → PRIM_RX=1 → CE high.
SPI: mode 0, 8 MHz, `spi_bus_initialize` on `pins->host`.

## Acceptance — CI

`./tools/ci_build_apps.sh` still green (component compiles once T10's app
exists; until then it must at least compile standalone — add it to T10's
build). Host tests untouched.

## Acceptance — hardware

Deferred to T10's checklist (register dump + two-unit ping).

## Out of scope

Protocol logic (validate/relay/queues — T16), auto-ACK/dynamic payloads,
multi-pipe support, channel hopping.
