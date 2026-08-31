# Raytac AN54LV-K15: A3 tone-antenna configuration end-to-end (Raytac ↔ DK pairs)

_Research for wayfinder ticket [#148](https://github.com/Metirionic/mars-cs-nrf54l/issues/148),
map [#146](https://github.com/Metirionic/mars-cs-nrf54l/issues/146). Follows
[2026-08-31-raytac-sdc-3-antenna-feasibility.md](https://github.com/Metirionic/mars-cs-nrf54l/blob/feat/wayfinder-147-research/docs/research/2026-08-31-raytac-sdc-3-antenna-feasibility.md)
(ticket #147), which established the controller side: `(NUM=3, MAX=4)` is a valid
SDC build and `cs_antenna_switch` multiplexing-mode 1 expresses the SKY13586 with
the antenna-index ↔ RF(3−i) permutation._

## Verdict

**Yes — an A3 tone-antenna configuration (Raytac ↔ DK) is valid and reachable
end-to-end, with no change to `common/antenna.c`, no firmware or host schema
change, and no buffer resize.**

- The Raytac↔DK pairings resolve to the two A3-bearing configurations the
  Bluetooth CS spec defines — `A3_B1` (config index 2) and `A1_B3` (index 5) —
  and `ANTENNA_MAPPING` **already holds the correct values** at those cells.
- The pairings negotiate N_AP = 3, which fits both sides only when the DK runs
  a `*_a1_4` preset; the working DK counterparts are `nrf54l15dk_peri_a1_4`
  (Raytac as initiator) and `nrf54l15dk_cent_a1_4` (Raytac as reflector).
- Host side: the COBS wire format, the mars-bluetooth-hci 0.13.2 schema, the
  firmware parse path, `g_serialized`, and the mars-acquisition decoders are all
  antenna-path-count-agnostic (runtime data under a spec-max bound of 4). Three
  paths need no schema change and cannot exceed the 4-path sizing that exists.

## Q1 — Which tone-antenna configurations involve A3, and what fills `[2][0]` / `[0][2]`?

The spec (Bluetooth CS HCI `tone_antenna_config` field; mirrored verbatim in
Zephyr's `enum bt_conn_le_cs_tone_antenna_config_selection`,
`zephyr/include/zephyr/bluetooth/conn.h:2073-2082` and the table comment above
it) allows exactly **eight** configs:

| Index | Paths | Dev A antennas | Dev B antennas | Config |
|-------|-------|----------------|----------------|--------|
| 0 | 1 | 1 | 1 | `A1_B1` |
| 1 | 2 | 2 | 1 | `A2_B1` |
| 2 | 3 | 3 | 1 | `A3_B1` |
| 3 | 4 | 4 | 1 | `A4_B1` |
| 4 | 2 | 1 | 2 | `A1_B2` |
| 5 | 3 | 1 | 3 | `A1_B3` |
| 6 | 4 | 1 | 4 | `A1_B4` |
| 7 | 4 | 2 | 2 | `A2_B2` |

**Only two configs exercise 3 antennas, and both pair a 3-antenna Dev with a
1-antenna Dev: `A3_B1` and `A1_B3`.** There is no `A3_B2`, `A3_B3`, `A3_B4`,
`A2_B3`, or `A4_B3` — the HCI field has no valid value for them, so any lookup
hit for those (Dev A, Dev B) combinations must remain a placeholder in a 4×4
mapping table.

`common/antenna.c`'s `ANTENNA_MAPPING[dev_a - 1][dev_b - 1]` already encodes
this. The two cells the Raytac↔DK pairings land on:

- **`[2][0]` → `BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A3_B1`** (Raytac initiator,
  3 local antennas, vs 1-antenna peer) — **already the real value, not a
  placeholder**.
- **`[0][2]` → `BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B3`** (Raytac reflector,
  1-antenna initiator) — **already the real value**.

The `A1_B1` entries at `[2][1..3]`, `[1][2..3]`, `[3][1..3]` are the standard
placeholder treatment for configs that don't exist in the spec (and for
`[1][3]`, `A2_B4`, which likewise exists nowhere). **The ticket's premise —
"[2][0] and [0][2] are currently placeholders" — was written before
#147; the verification above confirms the already-present values are exactly
the ones the spec calls for. `antenna.c` needs no change.**

Note the placeholder cells are not fully unreachable even so: a hypothetical
A4(Raytac)↔B3 pairing would also map to a nonexistent config — but no shipped
preset ever lands there (the board is 3-antenna; A4 Raytac presets do not
exist), same reasoning as the documented `docs/hardware.md` claim for
`[1][2]`, `[2][1]`, `[3][1]`.

## Q2 — Negotiation: what the pairings negotiate, and which DK presets pair with the Raytac

### The negotiation sequence (as the code runs it)

1. **Capabilities exchange** — `cs_initiator.c:570` calls
   `bt_le_cs_read_remote_supported_capabilities`; the SDC delivers the
   capabilities-complete event to **both** devices (#105). Each side's
   `remote_capabilities_cb` stores the peer's `num_antennas_supported` via
   `antenna_set_peer_count()` (`common/ble_callbacks.c:140`,
   `reflector/src/main.c:98`). Raytac advertises `num_antennas_supported = 3`,
   `max_antenna_paths_supported = MAX` (Kconfig values verbatim, #147);
   a DK A1 preset advertises 1 and its MAX.
2. **Config creation** — `bt_le_cs_create_config` (mode 2) carries no antenna
   fields; the config exists first, then gets its antenna geometry.
3. **Tone config selection** — each host derives the config *locally* from the
   role-aware lookup (`antenna_get_config_for_role`):
   - Raytac **initiator**: `ANTENNA_MAPPING[3-1][1-1]` = **`A3_B1`**
   - DK reflector: `ANTENNA_MAPPING[peer 3-1][local 1-1]` = `A3_B1`
   - DK initiator: `ANTENNA_MAPPING[local 1-1][peer 3-1]` = **`A1_B3`**
   - Raytac reflector: `ANTENNA_MAPPING[peer 1-1][local 3-1]` = `A1_B3`

   Both sides of a pairing compute the **same** config because the same table
   indexed as (A, B) is symmetric across the role swap. The initiator applies
   it in `bt_le_cs_set_procedure_parameters`
   (`tone_antenna_config_selection`, `cs_initiator.c:615-665+`); the reflector
   sets its own suggestion in RAS mode (`reflector/src/main.c:381-396`) and
   skips it in IPT mode (the initiator owns CS setup there).
4. **N_AP resolution** — the SDC resolves the path count from the tone config
   and validates it against **both** sides' advertised
   `MAX_ANTENNA_PATHS` — the #136/#139 rule: N_AP greater than either side's
   MAX is the HCI `0x0d` (−12) rejection. `cs_initiator.c:645` computes
   `antenna_paths = NUM_ANTENNAS × peer_count` for timing — and that product
   equals N_AP for **every one of the eight** valid configs (`A3_B1`/`A1_B3` →
   3), so the dynamic step-margin, subevent, and connection-interval sizing
   self-adapts to the 3-path procedures with no change. The procedure runs one
   subevent of 75 steps (72 channels + mode 0) as before; each mode-2 step
   now carries 3 PCT slots instead of 4, so events are **smaller**, not
   larger.

### What each Raytac ↔ DK pairing lands on

| Pairing | Tone config | N_AP | Constraint to hold |
|---------|-------------|------|--------------------|
| Raytac `cent` A3/4 (initiator) ↔ DK reflector | `A3_B1` (index 2) | 3 | both MAX ≥ 3 |
| DK initiator ↔ Raytac reflector, DK `cent` A1/4 | `A1_B3` (index 5) | 3 | both MAX ≥ 3 |

With the Raytac build at the #147-recommended `(NUM=3, MAX=4)`, N_AP = 3 ≤ 4 on
the Raytac side, so the **DK side must advertise MAX ≥ 3** — that is the entire
pairing constraint. This is the same shape as the documented Ezurio pairing
(`docs/hardware.md` "Tone-antenna configuration"): mismatched pairs negotiate
from the capabilities exchange, and the negotiated counts — not the heuristic —
decide. The Ezurio A2/4 ↔ DK A1/4 precedent negotiated `A2_B1` (N_AP 2) on both
sides; the Raytac↔DK pairs negotiate N_AP 3 the same way.

### DK bench counterparts: the `a1_4` family is the unique valid choice

- **`nrf54l15dk_peri_a1_4`** for the Raytac-initiator pair (`A3_B1`, N_AP 3 ≤
  MAX 4 ✓), and **`nrf54l15dk_cent_a1_4`** for the Raytac-reflector pair
  (`A1_B3`, N_AP 3 ≤ MAX 4 ✓). NUM=1 on the DK matches the only A3 configs the
  spec defines — a 1-antenna counterpart is *required* for `A3_B1`/`A1_B3`.
- `nrf54l15dk_peri_a2_2` / `nrf54l15dk_cent_a1_2` (MAX = 2): N_AP 3 exceeds the
  advertised MAX → `0x0d` rejection (#136 rule) — **not usable**.
- `nrf54l15dk_peri_a4_4` / `nrf54l15dk_cent_a4_4` (NUM = 4): the pairing lands
  on `ANTENNA_MAPPING[2][3]` / `[1][2]` — nonexistent configs, placeholder
  `A1_B1` → wrong geometry — **not usable**. (A DK also has no 4-physical-path
  need here: the Raytac's 3 antennas drive the config.)

The DK `a1_4` presets are also the natural bench fit operationally: they are the
presets the DK *4404 otherwise runs in every A1 pairing this repo ships, and
the same flash artifact covers both bench variants (IPT and RAS per #151's
matrix).

One nuance worth knowing, not acting on: before the capabilities exchange (or
if it fails), the heuristic fallback `peer = MAX / NUM` gives the Raytac side
`4 / 3 = 1` → still `A3_B1` / `A1_B3` — the Raytac side's heuristic happens to
land on the right cell. The DK side's heuristic (MAX 4 / NUM 1 = 4) would land
on `A1_B4` / `A4_B1`, which the controller rejects — but the initiator always
waits for the capabilities semaphore before computing the config
(`cs_initiator.c:574`), so the negotiated path is the only one that executes.

Residual bench unknown (queued into ticket #151, which already carries the
path↔RF ordering check): the full HCI flow on the SDC — config creation,
procedure-enable, per-step data for N_AP=3 — completing cleanly on real
hardware with a 3-antenna Raytac. Code and spec say yes; the bench is the
proof.

## Q3 — Host-side readiness for 3-path data

**Verdict: ready. Nothing to change; no wire-format bump, no lockstep, no
resize.**

- **mars-bluetooth-hci 0.13.2 schema** (`mars-bluetooth-hci/src/event/
  hci_le_cs/`): `antenna_path_count` is a runtime per-event field read off the
  wire (`subevent_result.rs:741,782`); per-step phase-correction / quality /
  extension-slot arrays are fixed at `MAX_ANTENNA_PATH_COUNT + 1` = 5 slots
  (`subevent_result.rs:237-241` — a spec-max bound, not a device count), so
  3-path data occupies slots 0..2 of the existing layout. The spec's antenna
  permutation tables **already include N_AP = 3** (`constants.rs`
  `TABLE_3`, 6 permutations, Core Spec Vol 6 Part H Tables 4.13–4.15), and
  `antenna_index(n_ap, path)` resolves logical path → physical antenna for
  `n_ap = 3`. Parsing and serializing are count-driven; nothing is hard-coded
  2 or 4. The 0.12.0 overflow incident was a *new mode1 field* shrinking `g_serialized`
  headroom — it is about format additions, not path-count changes; none occur
  here.
- **`g_serialized`** (`initiator/src/serialize.c`): fixed
  `CHUNK_SIZE (14 000 B) × 2 + 1000` sized against the *measured worst case*
  for a full 160-step event at 4 paths (13 968 B, crate 0.13.2). Events at 3
  paths are strictly within that bound on both wire encodings (fixed-array
  serialization is unchanged; count-driven serialization shrinks by one PCT +
  quality entry per mode-2 step). Carry-through for the implementation ticket:
  re-running the host serialize-size harness with a synthetic 3-path event is a
  cheap belt-and-braces confirmation, in the spirit of the 0.12.0 check.
- **Firmware parse path**: `num_antenna_paths` is read per-procedure from the
  subevent-result header and bounds every tone-info read (`common/subevent.c`,
  used by `cs_step_parse.c`) — the comment at
  `common/subevent.h:53-57` already documents that a procedure negotiating
  fewer paths than the compile-time max must not over-read. N_AP = 3 is that
  case, already handled.
- **mars-acquisition** (`mars/rust/mars-acquisition`): measurement storage is
  per-path `Option` slots at `MAX_ANTENNA_PATH_COUNT`
  (`measurement_data/store.rs`), distances/spectra/IR all count-driven; the
  decoders print `antenna_path_count`, mode-0 `packet_antenna`, and mode-2
  `antenna_permutation_index` as raw runtime fields (`examples/
  dump_subevents.rs`). **No decoder change is needed to carry 3-path data.**
- **Path → RF-connector labeling** (the fog item) resolves by composition, not
  code: the crate's `antenna_index(n_ap=3, permutation_index)` gives logical
  path → 0-based antenna index, and #147's documented board mapping
  (antenna index *i* ↔ RF(3−i) via the SKY13586 wiring) gives antenna index →
  RF connector. Composing them labels each PCT slot with its physical RF
  connector. That composed mapping belongs in the implementation ticket's board
  wiring notes (#150), and the bench verifies it carries RF1/RF2/RF3 plausibly
  (#151).

## What this ticket hands to the rest of the map

- **Ticket #149 (Decide the Raytac preset set)** — inputs from here: the
  preset needs `(NUM=3, MAX ≥ 3)`; `(NUM=3, MAX=4)` additionally preserves
  4-path `A2_B2` interop (Raytac↔Raytac/Ezurio-style pairs) and costs ~1 KB per
  CS context (SDC_MEM_CS, #147). A new path-local fragment `<paths>_path_
  3_local.conf` will be needed (no `*_3_local.conf` fragment exists today;
  `docs/hardware.md` "Kconfig fragments").
- **Ticket #150 (Implement board support)** — carry-throughs: host
  serialize-size harness re-run with a 3-path synthetic event; document the
  composed path→RF-connector mapping in the wiring notes (crate permutation
  tables × `RF(3−i)` board mapping).
- **Ticket #151 (Bench-verify in both roles)** — the negotiated tone config to
  expect in logs: `A3_B1` (Raytac initiator ↔ `nrf54l15dk_peri_a1_4`) and
  `A1_B3` (DK `nrf54l15dk_cent_a1_4` initiator ↔ Raytac reflector); N_AP 3,
  `num_antennas` advertised 3 / peer 1; the `a1_4` DK presets are the
  counterparts.

## Sources

- `zephyr/include/zephyr/bluetooth/conn.h:2062-2085` (NCS v3.4.0 tree) —
  tone-antenna-config enumeration and the 8-config table.
- `common/antenna.c` — `ANTENNA_MAPPING`, `antenna_get_config_for_role`,
  `antenna_set_peer_count`.
- `common/cs_initiator.c:570-670` — capabilities wait, config creation,
  procedure parameters, `antenna_paths = NUM × peer`.
- `common/ble_callbacks.c:136-150`, `reflector/src/main.c:92-105, 375-420` —
  capabilities handlers, reflector procedure-parameter suggestion.
- `common/subevent.c` + `common/subevent.h:53-57` — runtime `num_antenna_paths`
  in the firmware parse path.
- `initiator/src/serialize.c:25-40` — `g_serialized` sizing rationale
  (crate 0.13.2, 4-path worst case 13 968 B).
- `mars/rust/mars-bluetooth-hci` @ 0.13.2 — `constants.rs`
  (`MAX_ANTENNA_PATH_COUNT`, `TABLE_3`), `subevent_result.rs:741,782` (runtime
  `antenna_path_count`), `:256-257` (`antenna_index`).
- `mars/rust/mars-acquisition` — `measurement_data/store.rs`
  (`MAX_ANTENNA_PATH_COUNT`-sized storage), `examples/dump_subevents.rs`.
- `docs/hardware.md` — negotiation reference table, heuristic-vs-negotiated
  (Ezurio precedent), fragment naming.