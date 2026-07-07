## Behavior change for RAS users (#41)

This PR changes the initiator's CS procedure parameters for **all builds** —
not only the new IPT (`_ipt`) presets. This is intentional (maintainer
decision on #41): the new cadence speeds up measurements across every antenna
configuration. Any RAS fixture, calibration, or range baseline that assumed
the prior defaults must be re-baselined.

The four `cs_initiator_config` fields in `initiator/src/main.c` changed as
follows:

| Field | Prior (RAS-only) | Now (all builds) |
| --- | --- | --- |
| `procedure_phy` | `BT_LE_CS_PROCEDURE_PHY_1M` | `BT_LE_CS_PROCEDURE_PHY_2M` |
| `min_procedure_interval` | `20` | `10` |
| `max_procedure_interval` | `50` | `10` |
| `max_procedure_len` | `1000` | `144` |

`min/max_subevent_len` (16000) are unchanged.

### What this means in practice

- The procedure PHY is now **2M** (was 1M). 2M has different phase-ration
  behavior than 1M, so range/phase results are not directly comparable to
  1M-based baselines.
- The procedure interval is fixed at **10** connection events (was a
  20–50 window), so procedures run more often — faster measurement cadence.
- `max_procedure_len` is **144** (was 1000), matching the Nordic
  `ras_initiator` sample derivation
  (`acl_interval_units * (interval - 1) = 0x10 * 9 = 144`).

### Affected presets

All RAS-only initiator presets inherit the change, including:
`nrf54l15dk_cent_a1_1`, `ublox_cent_a1_1`, `ezurio_bl54l15u_cent_a2_4`,
`fanstel_bm15c_cent_a1_1`, and `nrf54l15tag_cent_a2_4`.

### Reflector

Unaffected. The reflector's procedure parameters
(`reflector/src/main.c`, RAS `#else` branch) are unchanged, and in IPT mode
the reflector does not set procedure parameters at all (the initiator drives
CS setup).

### Why not gate these behind `CONFIG_MARS_CS_INLINE_PCT`?

The cadence was deliberately made cross-cutting to speed up measurements for
every antenna/preset configuration, not just IPT. The in-code comment above
the config literal records this decision (see #41).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
