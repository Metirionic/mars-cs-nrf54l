# Raytac AN54LV-K15: SDC 3-antenna feasibility + SP3T GPIO mapping (NCS v3.4.0)

Research asset for [wayfinder #147](https://github.com/Metirionic/mars-cs-nrf54l/issues/147)
(map: [#146 Raytac AN54LV-K15 support](https://github.com/Metirionic/mars-cs-nrf54l/issues/146)).
Sources: the local NCS v3.4.0 install (`/home/aro/work/{nrf,nrfxlib,zephyr}`), the
Skyworks SKY13586-678LF datasheet (#203452), and the prior wayfinder research
[SDC antenna-config validation rules](https://github.com/Metirionic/mars-cs-nrf54l/blob/feat/cs-dynamic-timing/docs/research/2026-08-27-sdc-antenna-config-validation.md)
(wayfinder #136, `feat/cs-dynamic-timing` branch). This file covers only what #147 asks; it
defers to the #136 asset for the 0x0d/−12 validation rules, which apply unchanged here.

## TL;DR

The NCS v3.4.0 SDC accepts 3 antennas: `BT_CTLR_SDC_CS_NUM_ANTENNAS` ranges
1..`BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS` (which ranges 1–4), and Nordic's own CS
documentation includes a dedicated 3-antenna wiring example. Both `(NUM=3, MAX=3)`
and `(NUM=3, MAX=4)` are valid builds; `(3,4)` additionally preserves 4-path
(A2_B2 / A1_B4) operation. The capabilities events then carry
`num_antennas_supported = 3` and `max_antenna_paths_supported = MAX`.
The spec's only tone antenna configuration that exercises 3 antennas is
**A3_B1 (ACI 2, N_AP = 3)** — there is no A2_B3/A3_B2.

The SP3T switching is expressed with **`multiplexing-mode = <1>` and exactly two
`ant-gpios` ordered `<V2, V1>` = `<P0.04, P0.03>`** (binary index encoding). Because
the SKY13586 selects **RF3 when both control pins are low**, and the driver drives
all pins low for antenna index 0, no pin ordering can give a linear 0=RF1 mapping —
the physical mapping is necessarily permuted. With the pin order above it is the
clean inverse: **antenna index i ↔ RF port (3−i)** (0→RF3, 1→RF2, 2→RF1).

**Documented trap**: the NCS doc truth table for multiplexing mode
(`channel_sounding.rst`, "Antenna control … using multiplexing mode") is the
**transpose** of what the driver actually does — in NCS v3.4.0 and upstream `main`
(2026-05-06 doc rev) alike. Trust the code below, not that table.

## 1. Does the controller accept 3 antennas?

Yes — acceptance is explicit at three independent layers of NCS v3.4.0:

| Layer | Evidence |
|---|---|
| Kconfig | `nrf/subsys/bluetooth/controller/Kconfig:623-634` — `BT_CTLR_SDC_CS_NUM_ANTENNAS`, range **1..`BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS`**, help: "Must be equal to or less than BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS … When this value is not 1, the controller requires an antenna switching callback to function." |
| SDC config struct | `nrfxlib/softdevice_controller/include/sdc.h` `sdc_cfg_cs_cfg_t` — `num_antennas_supported` valid range **[1, max_antenna_paths_supported]**; `max_antenna_paths_supported` valid range **[1, 4]**. |
| Nordic docs | `nrfxlib/softdevice_controller/doc/channel_sounding.rst` — capability ceilings "Multiple Antenna paths: up to 4", "Multiple Antenna elements: up to 4", plus a dedicated **"Antenna control … with only 3 antennas"** devicetree example and truth table. |

Glue is automatic for `NUM > 1`: `BT_CTLR_SDC_CS_MULTIPLE_ANTENNA_SUPPORT` goes on
(auto-y when `NUM_ANTENNAS > 1`, `select`s `GPIO`), which compiles
`nrf/subsys/bluetooth/controller/cs_antenna_switch.c` and has `hci_driver.c:942-943`
register `cs_antenna_switch_func` via `sdc_cs_antenna_switch_callback_set()` and run
`cs_antenna_switch_init()`. `hci_driver.c:1255-1256` copies both Kconfigs into
`cfg.cs_cfg` (`sdc_cfg_set(SDC_CFG_TYPE_CS_CFG)`).

Build-time effects of `NUM=3` (`cs_antenna_switch.c:33-45`):

- The `cs_antenna_switch` devicetree node **must exist** — the file is compiled by
  `MULTIPLE_ANTENNA_SUPPORT` and `#error`s without it (same reason Fanstel/Minew,
  both NUM=1, have no node).
- In multiplexing mode: `BUILD_ASSERT(NUM_GPIOS >= 2)` — the two SP3T control pins
  satisfy this exactly.
- Compile-time RAM cost of `NUM_ANTENNAS` itself is nil; only `MAX_ANTENNA_PATHS`
  costs RAM (`SDC_MEM_CS`, `sdc.h:367-378`: +1024 bytes per antenna path per CS
  connection). `MAX=4` vs `MAX=3` therefore costs ~1 KB per CS context — noise
  against the 188 KB RAM these single-connection CS apps already fit.

Timing (same doc): the provided GPIO switching implementation takes ≤ 4 µs to set a
pin, so the external switch must switch within 6 µs inside the reported T_SW = 10 µs;
the SKY13586's sub-µs class is fine (bench confirms). With `NUM=3`,
`cs_antenna_switch.c`'s `cs_antenna_switch_func()` receives indices
**[0, NUM_ANTENNAS−1] = [0, 2]** (`cs_antenna_switch.h` / `sdc.h:415-422`
`sdc_cs_antenna_switch_callback_t` contract).

## 2. Accepted (NUM_ANTENNAS, MAX_ANTENNA_PATHS) combos

- `(3, 3)` and `(3, 4)` are both in range and valid. `(3, 2)` and lower are
  impossible (`NUM ≤ MAX`). `(4, 4)` is not meaningful for this board (3 physical
  antennas; the build cap would advertise 4).
- Validation of procedure parameters is unchanged from #136's rules: a tone
  antenna config is accepted only if **N_AP ≤ both sides' `max_antenna_paths_supported`**
  and **per-side antenna elements ≤ both sides' `num_antennas_supported`** (violations
  → HCI 0x0d / host −12). `NUM=3` advertises 3 antenna elements on the Raytac side, so
  A2_B2 (2 elements ≤ 3) also stays legal there — the antenna count is an upper bound,
  not an exact match requirement.
- `(3, 4)` is the practical choice: it keeps 4-path configs available (A2_B2 — the
  shape the existing `*_path_*_local.conf` 4-path fragments carry, including any
  Raytac↔Raytac-less 2↔2 runs) while A3_B1 needs only N_AP=3. `(3, 3)` works for an
  A3-only world but forfeits A2_B2. The preset-set decision picks between them
  (wayfinder #149); feasibility-wise both are accepted.
- `BT_RAS_MAX_ANTENNA_PATHS` must track MAX on both sides, as in every shipped
  fragment (#136 §7).

**Capabilities events**: the SDC reports the configured values verbatim —
`num_antennas_supported = 3`, `max_antenna_paths_supported = MAX` — in
`LE CS Read Local Supported Capabilities` and the capabilities-complete events
(bench-pinned 5-for-5 by #134/#135 that these fields equal the build's Kconfigs;
the exchange is controller-to-controller and both sides' hosts receive each other's
values, per #105). Affects nothing beyond those fields: `remote_capabilities_cb`
sees the peer as usual.

## 3. Which A3 tone-antenna configs exist (brief — detail is #148)

The Core spec's 8 antenna configuration indices (conn.h table, see #136 asset §2)
contain **no A3_B2 / A2_B3** — the only config exercising 3 antenna elements is
**A3_B1 (ACI 2) / A1_B3 (ACI 5), N_AP = 3**. `common/antenna.c`'s `ANTENNA_MAPPING`
already carries **real** (non-placeholder) entries at `[2][0]` = A3_B1 (Raytac
initiator, 3 antennas) and `[0][2]` = A1_B3 (Raytac reflector) — the
Raytac(3)↔DK(1-antenna) pairings. The negotiation path, heuristics, and
host-side 3-path readiness are wayfinder #148's question; nothing here blocks it.

## 4. `cs_antenna_switch.c` driver semantics (NCS v3.4.0)

`nrf/subsys/bluetooth/controller/cs_antenna_switch.c:74-98`:

- **`multiplexing-mode = <0>` (one-to-one)**: `gpio_dt_spec_table[antenna_number]`
  is driven active (previous one cleared) — one pin per antenna element, one-hot.
  3 antennas would need 3 dedicated select lines.
- **`multiplexing-mode = <1>` (multiplexed)**: binary encoding of the antenna index
  across the pins:

  ```c
  gpio_pin_set_dt(&gpio_dt_spec_table[0], antenna_number & (1 << 0)); /* ant-gpios[0] ← bit 0 */
  gpio_pin_set_dt(&gpio_dt_spec_table[1], antenna_number & (1 << 1)); /* ant-gpios[1] ← bit 1 */
  ```

  Note the table has only entries [0] and [1] in this mode — extra `ant-gpios`
  entries beyond two are ignored (though still pin-configured by
  `cs_antenna_switch_init()`); the mode supports at most 4 index codes.

The binding's `ant-gpios` doc comment ("<ANT1, ANT2, ANT3, ANT4>") describes the
**mode-0** convention; in mode 1 the entries are the bit-0 and bit-1 control lines,
not per-antenna ports.

### ⚠ The NCS doc's mode-1 truth table does not match the code

`channel_sounding.rst`'s "using multiplexing mode" table lists
Antenna 2 → P1.11=0/P1.12=1 and Antenna 3 → P1.11=1/P1.12=0 for
`ant-gpios = <P1.11, P1.12>`. The code produces the transpose (bit 0 lands on
`ant-gpios[0]` = P1.11): Antenna 2 → P1.11=1/P1.12=0, Antenna 3 → P1.11=0/P1.12=1.
Verified identical in NCS v3.4.0 (the tree we build) and in upstream `sdk-nrf`
`main` + `sdk-nrfxlib` `main` (2026-05-06 doc rev) — both sides unchanged since.
Consequence: **anyone wiring from the doc's table gets the two pins swapped
relative to reality.** The compiled driver is the ground truth; derive mappings
from the code excerpt above.

## 5. SKY13586-678LF truth table → Raytac node values

Skyworks #203452, Table 4 (VDD high; "1" = VCTL 1.6–3.6 V):

| V1 | V2 | Active port |
|----|----|-------------|
| 1  | 1  | RF2 |
| 1  | 0  | RF1 |
| 0  | 1  | RF2 |
| 0  | 0  | RF3 |

The map's bench note held for the V2-dominance (V2=1 → RF2 regardless of V1); the
datasheet adds the detail that matters: **(V1, V2) = (0, 0) selects RF3** — and the
driver drives all pins low for antenna index 0, so **antenna index 0 is forced onto
RF3 under every possible pin ordering**. A linear 0→RF1, 1→RF2, 2→RF3 mapping is
therefore not expressible by GPIO order alone. The two orderings give:

| `ant-gpios` order | idx 0 (pins low) | idx 1 | idx 2 | Shape |
|---|---|---|---|---|
| **`<P0.04 (V2), P0.03 (V1)>`** | RF3 | RF2 | RF1 | clean inverse: i ↔ RF(3−i) |
| `<P0.03 (V1), P0.04 (V2)>` | RF3 | RF1 | RF2 | cyclic shuffle |

### Deliverable: the Raytac `cs_antenna_switch` node

```devicetree
cs_antenna_switch: cs-antenna-config {
    status = "okay";
    compatible = "nordic,bt-cs-antenna-switch";
    ant-gpios = <&gpio0 4 (GPIO_ACTIVE_HIGH)>,   /* V2 — bit 0 */
                <&gpio0 3 (GPIO_ACTIVE_HIGH)>;   /* V1 — bit 1 */
    multiplexing-mode = <1>;
};
```

(Per the map's bench wiring: V1 = P0.03, V2 = P0.04.) With this order the subevent's
antenna paths enumerate in **reverse RF order — antenna index 0 = RF3, 1 = RF2,
2 = RF1**; document this permutation wherever path labels are interpreted
(`docs/hardware.md` Raytac wiring notes; bench #151 checks it: per-path I/Q should
show three distinct antennas with the ordering above, e.g. isolate one antenna with
an SP3T position / RF connector swap).

Alternatives, if the reversed order ever proves annoying:

- `<P0.03 (V1), P0.04 (V2)>` yields the (RF3, RF1, RF2) shuffle — no benefit.
- **A custom switch callback** (app-level `sdc_cs_antenna_switch_callback_set` +
  own pin init, dropping the DT node's driver path) could translate indices to any
  mapping including identity — but that departs from the repo's "NCS-owned
  `cs_antenna_switch.c` + devicetree" pattern used by every other board; not
  recommended unless the permutation causes real confusion downstream.
- Mode 0 is physically inexpressible for this board: one-hot needs one pin per
  antenna (≥3) while the SP3T exposes exactly two control inputs, and its partial
  states are outside the datasheet's Table 4 ("undefined state", no damage but no
  defined RF path).

Bench carry-through: the SKY13586's control levels are referenced to its own VDD
(3–5 V): logic "1" = 1.6–3.6 V, logic "0" = 0–0.4 V. The nRF54L15's GPIO output
high tracks the SoC VDD (VOH ≈ VDD − 0.4 V): at a 3.3 V module VDD that is ≈ 2.9 V
— comfortably "1"; at 1.8 V it is ≈ 1.4 V — **below** the switch's "1" window. The
module VDD rail the SP3T's V1/V2 lines are driven from (and the switch's own VDD
powering) is therefore a #152 rig check, as is the switch-within-6 µs timing
requirement.

## Source index

- `nrf/subsys/bluetooth/controller/Kconfig:611-641` — `MAX_ANTENNA_PATHS` (1–4),
  `NUM_ANTENNAS` (1..MAX), `MULTIPLE_ANTENNA_SUPPORT` auto-selects GPIO
- `nrf/subsys/bluetooth/controller/cs_antenna_switch.{c,h}` — mode 0/1 semantics,
  bit order (`c:78-82`), GPIO-count build assertions, index range contract
- `nrf/subsys/bluetooth/controller/hci_driver.c:942-943, 1255-1263` — callback
  registration + `sdc_cfg_set` glue
- `nrfxlib/softdevice_controller/include/sdc.h:415-422, 592-606` — callback
  prototype (index range [0, num_antennas−1]), `sdc_cfg_cs_cfg_t` ranges
- `nrfxlib/softdevice_controller/include/sdc.h:367-384` — `SDC_MEM_CS` (+1024/path)
- `nrfxlib/softdevice_controller/doc/channel_sounding.rst:163-300` — capabilities
  ceilings, multiple-antenna docs (incl. the 3-antenna example), T_SW/T_IPT timings,
  "first antenna is the BLE default"
- `nrf/dts/bindings/bluetooth/nordic,bt-cs-antenna-switch.yaml` — `multiplexing-mode`,
  `ant-gpios` binding (mode-0 ANTn naming caveat)
- `zephyr/include/zephyr/bluetooth/conn.h:2029-2081` — 8-ACI tone-config table
  (A3_B1 = ACI 2, N_AP 3), capabilities struct fields
- `common/antenna.c:18-31` — `ANTENNA_MAPPING` with real `[2][0]`/`[0][2]` A3 cells
- Skyworks [SKY13586-678LF datasheet #203452](https://www.skyworksinc.com/-/media/SkyWorks/Documents/Products/2401-2500/SKY13586-678LF_203452G.pdf)
  — Table 4 truth table, pinout, control-voltage windows
- Nordic doc-vs-code discrepancy re-checked against upstream `nrfconnect/sdk-nrf`
  and `nrfconnect/sdk-nrfxlib` (`main`, doc rev 2026-05-06) — both unchanged
- Prior research: `docs/research/2026-08-27-sdc-antenna-config-validation.md`
  (#0x0d/−12 rules, ACI table, capabilities-event pinning) on `feat/cs-dynamic-timing`
