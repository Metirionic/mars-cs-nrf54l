# Changelog
All notable changes to this project will be documented in this file. See [conventional commits](https://www.conventionalcommits.org/) for commit guidelines.

- - -
## [v1.6.1](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.6.1) - 2026-07-10
#### Bug Fixes
- (**initiator**) unshadow latest_local_steps in IPT procedure-complete block - ([f403ff7](https://github.com/Metirionic/mars-cs-nrf54l/commit/f403ff755ca2557f0cb1d5d9e4ef99726fb1bc99)) - Adrian Figueroa

- - -

## [v1.6.0](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.6.0) - 2026-07-10
#### Features
- share subevent populate + generalize IPT inline-result skeleton - ([201820d](https://github.com/Metirionic/mars-cs-nrf54l/commit/201820d34b25f423c1b5d9cdef77929c745df610)) - Adrian Figueroa, Claude
#### Style
- formatting - ([0fe36ee](https://github.com/Metirionic/mars-cs-nrf54l/commit/0fe36eec737d214ec40ebbd0a1ef218bcb075a3c)) - Adrian Figueroa

- - -

## [v1.5.1](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.5.1) - 2026-07-09
#### Bug Fixes
- (**reflector**) return on CS default settings failure - ([9820ca2](https://github.com/Metirionic/mars-cs-nrf54l/commit/9820ca24ca53c5867529e94b2b2c7a78244029df)) - AttilaRoemer

- - -

## [v1.5.0](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.5.0) - 2026-07-09
#### Features
- (**board**) add Minewsemi ME54BE01 (ME54BS01-nRF54L15) support - ([1d0e797](https://github.com/Metirionic/mars-cs-nrf54l/commit/1d0e797ce9d9b3eb89b95932f46d49837e4121c2)) - AttilaRoemer, Claude

- - -

## [v1.4.1](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.4.1) - 2026-07-08
#### Bug Fixes
- reflector antenna count - ([92ef0e2](https://github.com/Metirionic/mars-cs-nrf54l/commit/92ef0e26dba88266bea919c8527e7c87fd8a8b59)) - Adrian Figueroa
- consider antenna permutation index - ([2dc40d2](https://github.com/Metirionic/mars-cs-nrf54l/commit/2dc40d25b42094400d68d95711bf448ab062ccb0)) - Adrian Figueroa

- - -

## [v1.4.0](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.4.0) - 2026-07-07
#### Features
- Inline PCT support (v3.1.1) - ([6b7129c](https://github.com/Metirionic/mars-cs-nrf54l/commit/6b7129cca75885332616b40f5c06742b390852ee)) - Adrian Figueroa
#### Bug Fixes
- (**initiator**) return sem_local_steps token on IPT buffer-overflow path - ([d14340f](https://github.com/Metirionic/mars-cs-nrf54l/commit/d14340fa03eafa806e96041102fea68d4aeabb06)) - AttilaRoemer, Claude
- (**initiator**) gate async UART TX on completion to fix g_serialized reuse race - ([4302591](https://github.com/Metirionic/mars-cs-nrf54l/commit/4302591e127005bb059d0397523f73fb7d42a0fb)) - AttilaRoemer, Claude
- (**ipt**) derive MAX_TONE_COUNT from MARS_CS_IPT_MAX_ANTENNA_PATHS + 1 - ([769caf7](https://github.com/Metirionic/mars-cs-nrf54l/commit/769caf756fccf8d617dba762b719c7f8790aed41)) - AttilaRoemer, Claude
- (**ipt**) share MARS_CS_IPT_STEP_FRAMING_LEN with cs_step_parse walk loop - ([4d24e60](https://github.com/Metirionic/mars-cs-nrf54l/commit/4d24e60e5d22eed3ae8d3b95948b618f2b128207)) - AttilaRoemer, Claude
- (**ipt**) share CONFIG_BT_DEVICE_NAME so scan filter can't drift from ad name - ([54f0cc0](https://github.com/Metirionic/mars-cs-nrf54l/commit/54f0cc0a4dbad209b36d5d054fb9c3be5b6831b7)) - AttilaRoemer, Claude
- (**kconfig**) factor MARS_CS_INLINE_PCT menu into common/Kconfig - ([7f79e3d](https://github.com/Metirionic/mars-cs-nrf54l/commit/7f79e3d103ce4619b4e51e730e07acb170f73703)) - AttilaRoemer, Claude
- (**tag**) fix Fanstel uart20 sleep-state pin leak + review cleanups - ([dff37dd](https://github.com/Metirionic/mars-cs-nrf54l/commit/dff37ddea3de85e2074a3fb1cdfadb4a06630eec)) - AttilaRoemer, Claude
- (**tag**) use single UART (uart20) + RTT console for tag presets - ([1352d7b](https://github.com/Metirionic/mars-cs-nrf54l/commit/1352d7ba9fa9044bf1fe6237efb2a2af62d45cac)) - AttilaRoemer, Claude
- (**tag**) use nrf54l15tag base board for tag presets - ([5f70f9e](https://github.com/Metirionic/mars-cs-nrf54l/commit/5f70f9e8eb812f3e6f5cf96b883d601fc2a3cad0)) - AttilaRoemer, Claude
- tag setup - ([335109a](https://github.com/Metirionic/mars-cs-nrf54l/commit/335109a8d7b830258df2a93889db5af4ad1231f0)) - Adrian Figueroa
- board naming - ([ae89c39](https://github.com/Metirionic/mars-cs-nrf54l/commit/ae89c39e2aab62649d60ebdec70647b6c69ebea4)) - Adrian Figueroa
- build presets - ([cc49324](https://github.com/Metirionic/mars-cs-nrf54l/commit/cc49324b65c51fdad5c2d17b159a574a50e65c82)) - Adrian Figueroa
- use older NCS version (3.3.0) - ([c869264](https://github.com/Metirionic/mars-cs-nrf54l/commit/c8692643c60ccc10dbc919df6467290463d8693e)) - Adrian Figueroa
- slow down measurement speed for binary transfer - ([3cddd56](https://github.com/Metirionic/mars-cs-nrf54l/commit/3cddd56a6dcfdecdee381b969da58b28809f859e)) - Adrian Figueroa
- shorten connection interval - ([a092cc2](https://github.com/Metirionic/mars-cs-nrf54l/commit/a092cc2976b63683bf8cb810516557da181020ad)) - Adrian Figueroa
- variable names - ([2ffc439](https://github.com/Metirionic/mars-cs-nrf54l/commit/2ffc4395d0c4384d51201fd78b244ab093625ecc)) - Adrian Figueroa
- variable naming - ([56e0a99](https://github.com/Metirionic/mars-cs-nrf54l/commit/56e0a9925fa6e81b7a1474fa8a16fcb4381d9eb0)) - Adrian Figueroa
- formatting - ([562d219](https://github.com/Metirionic/mars-cs-nrf54l/commit/562d219d8bad8871158337ca3f0c47f699215b5a)) - Adrian Figueroa
#### Documentation
- (**cs**) record all-builds procedure-cadence decision (#41) - ([9011351](https://github.com/Metirionic/mars-cs-nrf54l/commit/901135189f62de776fe285eb845cfd180c12caa5)) - AttilaRoemer, Claude
- (**tag**) note nrf54l15tag.conf in EXTRA_CONF_FILE + Kconfig-fragment framing - ([ad126a4](https://github.com/Metirionic/mars-cs-nrf54l/commit/ad126a4298813ded2d8a41e4d756c16776bbbdca)) - AttilaRoemer, Claude
- (**tag**) finish TAG exceptions for chosen-node blankets + CI label - ([02f9438](https://github.com/Metirionic/mars-cs-nrf54l/commit/02f9438e8f0d1c39a0d2102c023c1a0cbaa00a09)) - AttilaRoemer, Claude
- (**tag**) fix RTT console claim, displayName rule, and architecture.md BOARD - ([e2c1ed4](https://github.com/Metirionic/mars-cs-nrf54l/commit/e2c1ed4c01c1bebbf2c3f5eecdfa97f7ade24743)) - AttilaRoemer, Claude
- (**tag**) fix stale CONFIG_SERIAL refs + multiplexing-mode blanket - ([0d4130b](https://github.com/Metirionic/mars-cs-nrf54l/commit/0d4130b1444789890a372aa560ddc552f64a4161)) - AttilaRoemer, Claude
- (**tag**) document nRF54L15 TAG UART wiring + FT232R requirement - ([e07ad9d](https://github.com/Metirionic/mars-cs-nrf54l/commit/e07ad9deaa07bd1835693cfcef642b847bfc2daf)) - AttilaRoemer, Claude
#### Refactoring
- (**tag**) drop redundant reflector CONFIG_SERIAL + 4-space overlays - ([4362db0](https://github.com/Metirionic/mars-cs-nrf54l/commit/4362db0027461a40f3fcffa33de8640027cde82f)) - AttilaRoemer, Claude
- (**tag**) address PR #50 review findings - ([0b06c4a](https://github.com/Metirionic/mars-cs-nrf54l/commit/0b06c4a7bc2cf4587d83e9ec8e166afb0f9655dc)) - AttilaRoemer, Claude
- deduplicate RAS/IPT code paths - ([b2845eb](https://github.com/Metirionic/mars-cs-nrf54l/commit/b2845ebbc04529bb9c51758eabf606d45543767b)) - Adrian Figueroa
#### Style
- apply clang-format + end-of-file-fixer to IPT files - ([8d40ecf](https://github.com/Metirionic/mars-cs-nrf54l/commit/8d40ecf4eab56eebfc660e4259279a637b5b380d)) - AttilaRoemer, Claude
- formatting - ([f14c58e](https://github.com/Metirionic/mars-cs-nrf54l/commit/f14c58e102e84f6073df17c9df25197dd994cc26)) - Adrian Figueroa
- adjust coding style - ([790757b](https://github.com/Metirionic/mars-cs-nrf54l/commit/790757b0b93a52a24e17df87c88e799abfe20142)) - Adrian Figueroa

- - -

## [v1.3.7](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.3.7) - 2026-07-06
#### Bug Fixes
- add review comment - ([f358438](https://github.com/Metirionic/mars-cs-nrf54l/commit/f358438358f5dd74b88f54f7134e0611385d9e31)) - Yan Wu

- - -

## [v1.3.6](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.3.6) - 2026-07-04
#### Bug Fixes
- hint 'west update' for partial workspaces; restore empty-ws_root guard - ([a775afc](https://github.com/Metirionic/mars-cs-nrf54l/commit/a775afca81673ac0ecb587009d0fa6d87dfd6fb3)) - AttilaRoemer, Claude
- validate NCS workspace and surface broken-west errors in find_ncs_dir - ([626a2da](https://github.com/Metirionic/mars-cs-nrf54l/commit/626a2da5e413e64acf28aa47751d29607c87b80b)) - AttilaRoemer, Claude
- add west topdir fallback to find_ncs_dir; drop NCS_DIR workaround - ([936c13a](https://github.com/Metirionic/mars-cs-nrf54l/commit/936c13adec1ddea949e490abcc459f78c13f73ec)) - AttilaRoemer, Claude

- - -

## [v1.3.5](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.3.5) - 2026-07-02
#### Bug Fixes
- detect existing release by HTTP status, not gh stderr prose - ([6579644](https://github.com/Metirionic/mars-cs-nrf54l/commit/6579644940265b62883285008d9ac586d3c9ad82)) - AttilaRoemer, Claude
- make release step idempotent so re-runs can't duplicate notes - ([00e8c37](https://github.com/Metirionic/mars-cs-nrf54l/commit/00e8c378b43f67510e6b97e0f76686e1fa3245c5)) - AttilaRoemer, Claude

- - -

## [v1.3.4](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.3.4) - 2026-07-01
#### Bug Fixes
- gate bump detection on semver tag shape, not cog prose - ([5a20402](https://github.com/Metirionic/mars-cs-nrf54l/commit/5a20402b9093cbdbc2d0df67ded1b4fa1fc88bf0)) - AttilaRoemer, Claude
- detect no-bump from cog dry-run output, not exit code - ([d48d2ff](https://github.com/Metirionic/mars-cs-nrf54l/commit/d48d2ffa20954185d01a26702aa350991532edca)) - AttilaRoemer, Claude
#### Documentation
- point west flash at the preset build dir in build-from-source - ([8f1e23d](https://github.com/Metirionic/mars-cs-nrf54l/commit/8f1e23d22e18cff8ea160bb7fbd0c26a81eb3834)) - AttilaRoemer, Claude
- scope flash-quickstart out-of-scope bullet to prebuilt flashing - ([547ccdb](https://github.com/Metirionic/mars-cs-nrf54l/commit/547ccdb63ff0b52b3fc180e7bdff15677477447c)) - AttilaRoemer, Claude
- rework README into landing page with flash/build entry links - ([cc7ee36](https://github.com/Metirionic/mars-cs-nrf54l/commit/cc7ee36f84b245a1ff87cd949e0c125485e2f782)) - AttilaRoemer, Claude
- add build-from-source guide - ([e05ce5d](https://github.com/Metirionic/mars-cs-nrf54l/commit/e05ce5d1276ac6a445c4a4d6557d6dc685bbe94f)) - AttilaRoemer, Claude

- - -

## [v1.3.3](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.3.3) - 2026-07-01
#### Bug Fixes
- use custom cocogitto changelog template to avoid duplicated commit SHAs in version headers - ([651e09d](https://github.com/Metirionic/mars-cs-nrf54l/commit/651e09d6b07441675c5db06ee66dde2b13b9ed8b)) - test, Claude

- - -

## [v1.3.2](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.3.2) - 2026-07-01
#### Bug Fixes
- resolve measurement issue on initiator with combination of 1 antenna and 4 paths - ([df43fed](https://github.com/Metirionic/mars-cs-nrf54l/commit/df43fed03a8801c722a71b34e551b32fd72aefe3)) - Yan Wu
#### Documentation
- correct cog bump --auto enforcement claim in contributing guide - ([27ac3c5](https://github.com/Metirionic/mars-cs-nrf54l/commit/27ac3c503d01d514aa5415cf559655c044309804)) - AttilaRoemer, Claude
- add contributing guide for commit and lint tooling - ([fefe368](https://github.com/Metirionic/mars-cs-nrf54l/commit/fefe36879d1f225f8081031547a9e45d99864539)) - AttilaRoemer, Claude
#### Refactoring
- add review comment - ([148968c](https://github.com/Metirionic/mars-cs-nrf54l/commit/148968c33b17f5cff2ab9b5eac1ab26ef853e23b)) - Yan Wu
- add review comment - ([c6ed608](https://github.com/Metirionic/mars-cs-nrf54l/commit/c6ed608d76020be57f31aef97495e9135ffc5abb)) - Yan Wu
- fix lint issues - ([74dd304](https://github.com/Metirionic/mars-cs-nrf54l/commit/74dd304a74b9e4367d5ea7d09255a1d88335f05c)) - Yan Wu

- - -

## [v1.3.1](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.3.1) - 2026-06-30
#### Bug Fixes
- add trailing newline to architecture.md - ([ef22678](https://github.com/Metirionic/mars-cs-nrf54l/commit/ef226788540af45b1407fd725ed2d3001edd1f70)) - AttilaRoemer, Claude
#### Documentation
- add architecture overview for the firmware - ([bfc2c3b](https://github.com/Metirionic/mars-cs-nrf54l/commit/bfc2c3bb1fc7f35bd888a486aeff94e33ff1d33e)) - AttilaRoemer, Claude
- fix hardware-reference claims and make it the preset-table source - ([2503dd3](https://github.com/Metirionic/mars-cs-nrf54l/commit/2503dd36071f5a7cfd190e5c5b2604a125f2c8a6)) - AttilaRoemer, Claude
- add hardware reference for boards, UARTs, and antenna/path presets - ([c04a208](https://github.com/Metirionic/mars-cs-nrf54l/commit/c04a20866f35506ea1f4b5c83f9beb4aea941378)) - AttilaRoemer, Claude
- dedupe CS-procedure-running note in flash-quickstart - ([1f03061](https://github.com/Metirionic/mars-cs-nrf54l/commit/1f03061383cb151a5fc19df4763e9a0463628d69)) - AttilaRoemer, Claude
- add flash-quickstart guide for prebuilt-firmware evaluators - ([45863ab](https://github.com/Metirionic/mars-cs-nrf54l/commit/45863abee389a2fbfb18b4745b7ddae94ee25e61)) - AttilaRoemer, Claude

- - -

## [v1.3.0](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.3.0) - 2026-06-30
#### Features
- add install codifier + onboarding-verification CI - ([2e11133](https://github.com/Metirionic/mars-cs-nrf54l/commit/2e1113333b93866dd713c0fa62453cdb344cfed3)) - AttilaRoemer, Claude

- - -

## [v1.2.0](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.2.0) - 2026-06-16
#### Features
- use latest mars-common - ([2a6264e](https://github.com/Metirionic/mars-cs-nrf54l/commit/2a6264eadff58389d61d867a25a944a0eacdebc4)) - Adrian Figueroa

- - -

## [v1.1.0](https://github.com/Metirionic/mars-cs-nrf54l/releases/tag/v1.1.0) - 2026-06-12
#### Features
- add automated release notes with changelog and build info - ([bf4386b](https://github.com/Metirionic/mars-cs-nrf54l/commit/bf4386bf0ffd99f4231ddc49a124a37bcafbe55d)) - Adrian Figueroa
#### Bug Fixes
- formatting - ([65439a2](https://github.com/Metirionic/mars-cs-nrf54l/commit/65439a21df0aeafe2234592647c3557486346f95)) - Adrian Figueroa
- remove monorepo mode from cog.toml and use v-prefixed tags - ([f1ed9b1](https://github.com/Metirionic/mars-cs-nrf54l/commit/f1ed9b11c3ba453dbc053ecc568994c59921e6c6)) - Adrian Figueroa
- logging info - ([b00145f](https://github.com/Metirionic/mars-cs-nrf54l/commit/b00145fba1b121b96cb13daf9d07d7347f816446)) - Adrian Figueroa

- - -


Changelog generated by [cocogitto](https://github.com/cocogitto/cocogitto).