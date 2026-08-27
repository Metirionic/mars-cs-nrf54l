# SDC antenna-config validation rules (NCS v3.4.0)

Research asset for [wayfinder #136](https://github.com/Metirionic/mars-cs-nrf54l/issues/136)
(map: [#133 max-antenna-path selection for asymmetric CS pairs](https://github.com/Metirionic/mars-cs-nrf54l/issues/133)).
Sources: the local NCS v3.4.0 install (`/home/aro/work/{zephyr,nrfxlib,nrf}`), the
#134/#135 bench resolutions, and Nordic DevZone threads. NCS v3.4.0's SDC build is
`c8da3098f9f034a44b6ebad30819cc0cea51da47` (ll_version 0x11, 2026-07-01) — closed-source
`.a` for nRF54L, so controller-internal rules are pinned from headers + glue + bench
behavior, and corroborated by DevZone.

## TL;DR

The SDC validates `tone_antenna_config_selection` **at LE CS Set Procedure Parameters
(0x2093), on every device that issues the command — initiator and reflector alike** —
against the CS capabilities **both sides advertised during the capability exchange**.
Those advertised capabilities are the build's Kconfigs
(`CONFIG_BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS`, `CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS`), not
the physical hardware. A config whose antenna-path count exceeds either side's
advertised `max_antenna_paths_supported` is rejected with
**HCI 0x0d (Rejected Due To Limited Resources)**, which Zephyr's host surfaces as
**-ENOMEM (-12)** — a misleading errno, not a heap problem.

Consequence for the stock v1.12.0 1↔2 pair: the a1_1 initiator advertises
`max_antenna_paths=1`, so A1_B2 (N_AP=2) is rejected **on both devices** — including the
2-antenna reflector whose own build supports 4 paths. The fix is build-level: raise
`MAX_ANTENNA_PATHS` on both sides (the existing `a1_4` preset shape), keep
`NUM_ANTENNAS` at the physical antenna count.

## 1. The two Kconfigs and where they land

`nrf/subsys/bluetooth/controller/Kconfig:611-641` (NCS v3.4.0):

| Kconfig | Default | Range | Meaning |
|---|---|---|---|
| `BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS` | 1 | 1–4 | Max antenna paths (N_AP) the controller supports. "The number of antenna paths used during channel sounding depends on the tone antenna configuration index selected in the procedure parameters. For instance, an A2_B2 configuration requires four antenna paths." Reducing it reduces RAM per CS context. |
| `BT_CTLR_SDC_CS_NUM_ANTENNAS` | 1 | 1–`MAX_ANTENNA_PATHS` | Max antennas the **local** controller supports. ">1 requires an antenna switching callback" (devicetree `cs-antenna-switch` or `sdc_cs_antenna_switch_callback_set`). |

Glue: `nrf/subsys/bluetooth/controller/hci_driver.c:1255-1263` copies both into
`cfg.cs_cfg` and calls `sdc_cfg_set(SDC_CFG_TYPE_CS_CFG)`. The receiving struct
`nrfxlib/softdevice_controller/include/sdc.h` `sdc_cfg_cs_cfg_t`:

```c
uint8_t max_antenna_paths_supported;   /* valid range [1, 4], default 1 */
uint8_t num_antennas_supported;        /* valid range [1, max_antenna_paths_supported], default 1 */
```

These are the only antenna knobs the controller has — one path ceiling and one
antenna-element count for the local device, no per-role split.

## 2. The tone-antenna-config enum, as installed

`zephyr/include/zephyr/bluetooth/conn.h:2073` `enum bt_conn_le_cs_tone_antenna_config_selection`,
values 0–7 via `BT_HCI_OP_LE_CS_ACI_0..7` (`hci_types.h:2856+`). The conn.h doc table:

| Config | N_AP (total paths) | Dev A antennas | Dev B antennas | Group |
|---|---|---|---|---|
| 0 | 1 | 1 | 1 | 1:1 |
| 1 | 2 | 2 | 1 | N:1 |
| 2 | 3 | 3 | 1 | N:1 |
| 3 | 4 | 4 | 1 | N:1 |
| 4 | 2 | 1 | 2 | 1:N — **A1_B2** |
| 5 | 3 | 1 | 3 | 1:N — A1_B3 |
| 6 | 4 | 1 | 4 | 1:N — A1_B4 |
| 7 | 4 | 2 | 2 | 2:2 — A2_B2 |

Dev A = initiator side, Dev B = reflector side. **Config 4 = A1_B2 = N_AP 2** — the map's
decode was correct. Note the spec's 8 ACIs cover only 1:1, N:1, 1:N and 2:2: there is
**no config for a 2↔3 (or 2↔4, 3↔2, 3↔4…) antenna pair** — the `ANTENNA_MAPPING` table
downgrades those cells to A1_B1.

## 3. What the controller advertises — and that it derives from the Kconfigs

The LE CS capabilities (`sdc_hci_cmd_le.h` `sdc_hci_cmd_le_cs_read_local_supported_capabilities_return_t`,
fields `num_antennas_supported` / `max_antenna_paths_supported`; same fields in the remote
capabilities event `SDC_HCI_SUBEVENT_LE_CS_READ_REMOTE_SUPPORTED_CAPABILITIES_COMPLETE = 0x2c`)
report exactly the configured values. The SDC is closed-source, but the bench data pins it
5-for-5: every build's "Local antennas: X, paths: Y" log and every "peer antennas: N"
observation (#134, #135) matched that build's Kconfigs — e.g. `cent_a1_1` reports
"antennas 1, paths 1", `tag_peri_a2_4` reports "antennas 2, paths 4".

The capability exchange is controller-to-controller (the app only calls
`bt_le_cs_read_remote_supported_capabilities`, `common/cs_initiator.c:564`; it never calls
`bt_le_cs_write_cached_remote_supported_capabilities`). Both SDCs therefore hold each
other's advertised caps when procedure parameters are validated.

## 4. Where the 0x0d / -12 rejection comes from

The SDC's own doc for `sdc_hci_cmd_le_cs_set_procedure_params` (`sdc_hci_cmd_le.h:~11760`,
Core v6.3 §7.8.140 extract) lists the first error condition as:

> **The parameters exceed the CS capabilities or any coexistence constraints →
> Rejected Due To Limited Resources (0x0D).**

That is the observed failure: opcode `0x2093`, status `0x0d`. Zephyr's host then converts
it in `bt_hci_cmd_send_sync` (`zephyr/subsys/bluetooth/host/hci_core.c:492-504`):

```c
case BT_HCI_ERR_INSUFFICIENT_RESOURCES:   /* 0x0d, hci_types.h:4500 */
    return -ENOMEM;                       /* -12 */
```

So `err -12` is **the controller rejecting the parameters**, not an allocation failure —
do not chase heap when you see it.

A second, distinct validation exists on the same command: the number of bits set in
`preferred_peer_antenna` must be ≥ the peer-side antenna elements denoted by the config;
violating it gives **0x12 / -EINVAL (-22)**, not 0x0d (Nordic-confirmed in
[DevZone 120716](https://devzone.nordicsemi.com/f/nordic-q-a/120716/multi-antenna-channel-sounding)).
Our app satisfies this rule by construction (`antenna_get_peer_mask()` = one bit per peer
antenna, `common/antenna.c:115`), which matches the bench: we only ever see 0x0d.

## 5. The validation rule, as evidenced by the bench

Rule (inferred, consistent with every observation): a config is accepted only if, **for
both sides of the link**, its N_AP ≤ that side's advertised `max_antenna_paths_supported`
and its per-side antenna elements ≤ that side's advertised `num_antennas_supported`.

Bench matrix (#134 stock v1.12.0, #135 v1.12.0 + rebuilt initiator):

| Build (Kconfigs) | Config tried | N_AP / antennas needed | Verdict |
|---|---|---|---|
| DK `cent_a1_1` (paths 1, ant 1) as initiator | A1_B2 (4) | 2 paths, A1 B2 | **reject 0x0d** — N_AP 2 > local paths 1 |
| TAG `peri_a2_4` (paths 4, ant 2) as reflector | A1_B2 (4) | 2 paths, A1 B2 | **reject 0x0d** — N_AP 2 > *remote-advertised* paths 1 |
| TAG `cent_a2_4` (paths 4, ant 2) as initiator | A2_B1 (1) | 2 paths, A2 B1 | **accept** |
| DK `peri_a1_4` (paths 4, ant 1) as reflector | A2_B1 (1) | 2 paths, A2 B1 | **accept** — remote B-side 1 antenna = local 1 |
| DK `cent_a1_4` (paths 4, ant 1) as initiator | A1_B2 (4) | 2 paths, A1 B2 | **accept** |
| TAG `peri_a2_4` (paths 4, ant 2) as reflector | A1_B2 (4) | 2 paths, A1 B2 | **accept** |

The controlled experiment is rows 2 vs 6: the **same TAG build** rejects A1_B2 when paired
with an a1_1 initiator and accepts it when paired with an a1_4 initiator. Nothing else
changed (same config value, same dynamic timing, same flow), so the reflector's SDC must
be validating against the **remote's advertised capabilities**, not just its own build.
Row 1 shows the same in mirror: the a1_1 initiator rejects a config its own antenna count
(A1) would allow, because N_AP 2 exceeds its build's path ceiling.

What the bench does *not* exercise: a num_antennas violation (per-side elements exceeding
the advertised count) — no tested config tripped it, so the antenna-element half of the
rule is inferred (from `sdc_cfg_cs_cfg_t` semantics + the DevZone-confirmed
preferred-peer-antenna rule), not observed. The v1.11.0 note "peer antennas > local →
rejected" was the *paths* violation misread through the antenna lens.

## 6. App-side selection (`common/antenna.c`)

`antenna_get_config_for_role()` (`common/antenna.c:97-108`) picks the config purely from
**antenna-element counts**: local = `CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS`, peer = the
negotiated `num_antennas_supported` (pre-negotiation fallback:
`MAX_ANTENNA_PATHS / NUM_ANTENNAS`). `ANTENNA_MAPPING[local-1][peer-1]` for the
initiator; `[peer-1][local-1]` for the reflector. **It never consults
`MAX_ANTENNA_PATHS`** — the app emits whatever config the antenna counts imply and lets
the SDC gate it. That is exactly why stock v1.12.0 selects A1_B2 for the 1↔2 pair on both
devices and dies at procedure-parameter validation on both devices.

Both sides call `bt_le_cs_set_procedure_parameters` with their role-derived config
(`common/cs_initiator.c:618+`, `reflector/src/main.c:384+`), so both SDCs run the same
validation — matching the bench, where both sides failed (or both succeeded) together.

## 7. Nordic's own recipe for the 1↔2 pair

[DevZone 126048](https://devzone.nordicsemi.com/f/nordic-q-a/126048/adjust-ncs-channel-sounding-samples-to-support-multiple-rf-paths)
(Nordic staff, tested): initiator 1 antenna → `MAX_ANTENNA_PATHS=2, NUM_ANTENNAS=1`;
reflector 2 antennas → `MAX_ANTENNA_PATHS=2, NUM_ANTENNAS=2`; select A1_B2 with a 2-bit
`preferred_peer_antenna`; raise `BT_RAS_MAX_ANTENNA_PATHS` to match; grow subevent length
and connection interval for the extra path. Reference commit
`eriksandgren/sdk-nrf@df798b95`. I.e. **the path ceiling is raised on the 1-antenna side
too** — exactly the shape of our `4_path_1_local.conf` fragment and the #135 a1_4 result.

## 8. Constraints for the fix plan (#137)

1. **Both sides need `MAX_ANTENNA_PATHS` ≥ the highest N_AP any negotiated pair will
   select.** For 1↔4 and 4↔1 pairs that means 4 on both sides. `NUM_ANTENNAS` stays at
   the physical count (raising it would demand an antenna-switch callback the hardware
   doesn't have).
2. **`BT_RAS_MAX_ANTENNA_PATHS` must track the path count** (it sizes the RAS host-side
   buffers); every shipped fragment already sets it alongside the controller configs.
3. **RAM cost per CS context grows with paths** (Kconfig help). The 4-path builds fit
   (v1.11.0's A2_B2 4-path link and #135's a1_4 build were bench-verified), but any
   preset-matrix change should keep the 96%-RAM OOM history (#100-era v3.1.1 work) in
   mind.
4. **The 2↔3 antenna case has no valid ACI** — if the destination ever grows beyond
   1↔N / N↔1 / 2↔2 pairs, selection must downgrade explicitly (the mapping table already
   does, to A1_B1).
5. **Host-side wire implications**: N_AP > 1 changes the per-subevent result payload
   (per-path PCT slots / antenna permutation fields). The map's fog item on
   `mars-bluetooth-hci` schema + `mars-acquisition` decode applies once the fix plan
   settles which presets ship with which paths.
6. Footnote: a user reported an NCS 3.3.0-era SDC crash ("ASSET 131,1141") with a working
   3.1.1 multi-path config, unresolved in-thread; not observed on v3.4.0 (clean 876 s+
   sessions in #135), but worth knowing the multi-path area has had regressions between
   SDK versions.

## Source index

- `zephyr/include/zephyr/bluetooth/conn.h:2049-2082` — config table + enum (values 0–7)
- `zephyr/include/zephyr/bluetooth/hci_types.h:2856+`, `:4500` — ACI values; 0x0d = INSUFFICIENT_RESOURCES
- `zephyr/subsys/bluetooth/host/hci_core.c:492-504` — 0x0d → -ENOMEM (-12)
- `zephyr/subsys/bluetooth/host/cs.c:1127` — `bt_le_cs_set_procedure_parameters` passthrough
- `nrf/subsys/bluetooth/controller/Kconfig:611-641` — the two Kconfigs + help
- `nrf/subsys/bluetooth/controller/hci_driver.c:1255-1263` — glue to `sdc_cfg_set`
- `nrfxlib/softdevice_controller/include/sdc.h:139-142`, `sdc_cfg_cs_cfg_t` — defaults, ranges
- `nrfxlib/softdevice_controller/include/sdc_hci_cmd_le.h` — Set-Procedure-Params error table; capabilities structs
- `nrfxlib/softdevice_controller/doc/channel_sounding.rst` — capability ceilings (4 paths, 4 elements), antenna switching, T_SW
- Bench evidence: [#134](https://github.com/Metirionic/mars-cs-nrf54l/issues/134), [#135](https://github.com/Metirionic/mars-cs-nrf54l/issues/135) resolutions
- DevZone: [120716](https://devzone.nordicsemi.com/f/nordic-q-a/120716/multi-antenna-channel-sounding) (preferred-peer-antenna → 0x12),
  [126048](https://devzone.nordicsemi.com/f/nordic-q-a/126048/adjust-ncs-channel-sounding-samples-to-support-multiple-rf-paths) (asymmetric 1↔2 recipe, -ENOMEM),
  [124064](https://devzone.nordicsemi.com/f/nordic-q-a/124064/channel-sounding-and-more-antenna-paths) (4-path 2↔2)