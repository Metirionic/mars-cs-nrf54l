# Changelog
All notable changes to this project will be documented in this file. See [conventional commits](https://www.conventionalcommits.org/) for commit guidelines.

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