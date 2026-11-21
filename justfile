# SPDX-License-Identifier: MIT

set positional-arguments := true
set shell := ["bash", "-euo", "pipefail", "-c"]

# Use the repository's Python environment and the installed Rust tools.

export PATH := justfile_directory() + "/.venv/bin:" + env_var_or_default("CARGO_HOME", env_var("HOME") + "/.cargo") + "/bin:" + env_var("PATH")

# List available recipes.
default:
    @just --list

# Run lint, documentation, and SDK version checks.
check: lint check-docs check-ncs-version

# Run all pre-commit hooks; formatters may update files.
lint:
    pre-commit run --all-files

# Check documentation links and preset references.
check-docs:
    bash scripts/check-docs.sh

# Check SDK version consistency.
check-ncs-version:
    bash scripts/check-ncs-version.sh

# Install the native toolchain, including Rust.
install-toolchain:
    bash scripts/install-toolchain.sh --with-rust

# Build all release initiator presets.
build-release-initiator:
    bash ci/build.sh --target initiator --preset nrf54l15dk_cent_a1_4,nrf54l15tag_cent_a2_4,ublox_cent_a1_4,ezurio_bl54l15u_cent_a2_4,fanstel_bm15c_cent_a1_4,minew_me54be01_cent_a1_4,raytac_an54lv_k15_cent_a3_4,insight_isp2454_cent_a1_4,ophelia4ev_cent_a1_4,kaga_ec4l15ba1_cent_a1_4,nrf54l15dk_cent_a1_4_ipt,nrf54l15tag_cent_a2_4_ipt,ublox_cent_a1_4_ipt,ezurio_bl54l15u_cent_a2_4_ipt,fanstel_bm15c_cent_a1_4_ipt,minew_me54be01_cent_a1_4_ipt,raytac_an54lv_k15_cent_a3_4_ipt,insight_isp2454_cent_a1_4_ipt,ophelia4ev_cent_a1_4_ipt,kaga_ec4l15ba1_cent_a1_4_ipt --release-dir release

# Build all release reflector presets.
build-release-reflector:
    bash ci/build.sh --target reflector --preset nrf54l15dk_peri_a1_4,nrf54l15tag_peri_a2_4,ezurio_bl54l15u_peri_a2_4,fanstel_bm15c_peri_a1_4,ublox_peri_a1_4,minew_me54be01_peri_a1_4,raytac_an54lv_k15_peri_a3_4,insight_isp2454_peri_a1_4,ophelia4ev_peri_a1_4,kaga_ec4l15ba1_peri_a1_4,nrf54l15dk_peri_a1_4_ipt,nrf54l15tag_peri_a2_4_ipt,ezurio_bl54l15u_peri_a2_4_ipt,fanstel_bm15c_peri_a1_4_ipt,ublox_peri_a1_4_ipt,minew_me54be01_peri_a1_4_ipt,raytac_an54lv_k15_peri_a3_4_ipt,insight_isp2454_peri_a1_4_ipt,ophelia4ev_peri_a1_4_ipt,kaga_ec4l15ba1_peri_a1_4_ipt --release-dir release

# Build nordic-dk initiator RAS firmware.
build-nordic-dk-initiator:
    bash ci/build.sh --target initiator --preset nrf54l15dk_cent_a1_4

# Flash the existing nordic-dk initiator RAS build.
flash-nordic-dk-initiator:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_nrf54l15dk_cent_a1_4

# Build and flash nordic-dk initiator RAS firmware.
build-flash-nordic-dk-initiator: build-nordic-dk-initiator
    @just flash-nordic-dk-initiator

# Build nordic-dk initiator IPT firmware.
build-nordic-dk-initiator-ipt:
    bash ci/build.sh --target initiator --preset nrf54l15dk_cent_a1_4_ipt

# Flash the existing nordic-dk initiator IPT build.
flash-nordic-dk-initiator-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_nrf54l15dk_cent_a1_4_ipt

# Build and flash nordic-dk initiator IPT firmware.
build-flash-nordic-dk-initiator-ipt: build-nordic-dk-initiator-ipt
    @just flash-nordic-dk-initiator-ipt

# Build nordic-dk reflector RAS firmware.
build-nordic-dk-reflector:
    bash ci/build.sh --target reflector --preset nrf54l15dk_peri_a1_4

# Flash the existing nordic-dk reflector RAS build.
flash-nordic-dk-reflector:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_nrf54l15dk_peri_a1_4

# Build and flash nordic-dk reflector RAS firmware.
build-flash-nordic-dk-reflector: build-nordic-dk-reflector
    @just flash-nordic-dk-reflector

# Build nordic-dk reflector IPT firmware.
build-nordic-dk-reflector-ipt:
    bash ci/build.sh --target reflector --preset nrf54l15dk_peri_a1_4_ipt

# Flash the existing nordic-dk reflector IPT build.
flash-nordic-dk-reflector-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_nrf54l15dk_peri_a1_4_ipt

# Build and flash nordic-dk reflector IPT firmware.
build-flash-nordic-dk-reflector-ipt: build-nordic-dk-reflector-ipt
    @just flash-nordic-dk-reflector-ipt

# Build ezurio initiator RAS firmware.
build-ezurio-initiator:
    bash ci/build.sh --target initiator --preset ezurio_bl54l15u_cent_a2_4

# Flash the existing ezurio initiator RAS build.
flash-ezurio-initiator:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_ezurio_bl54l15u_cent_a2_4

# Build and flash ezurio initiator RAS firmware.
build-flash-ezurio-initiator: build-ezurio-initiator
    @just flash-ezurio-initiator

# Build ezurio initiator IPT firmware.
build-ezurio-initiator-ipt:
    bash ci/build.sh --target initiator --preset ezurio_bl54l15u_cent_a2_4_ipt

# Flash the existing ezurio initiator IPT build.
flash-ezurio-initiator-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_ezurio_bl54l15u_cent_a2_4_ipt

# Build and flash ezurio initiator IPT firmware.
build-flash-ezurio-initiator-ipt: build-ezurio-initiator-ipt
    @just flash-ezurio-initiator-ipt

# Build ezurio reflector RAS firmware.
build-ezurio-reflector:
    bash ci/build.sh --target reflector --preset ezurio_bl54l15u_peri_a2_4

# Flash the existing ezurio reflector RAS build.
flash-ezurio-reflector:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_ezurio_bl54l15u_peri_a2_4

# Build and flash ezurio reflector RAS firmware.
build-flash-ezurio-reflector: build-ezurio-reflector
    @just flash-ezurio-reflector

# Build ezurio reflector IPT firmware.
build-ezurio-reflector-ipt:
    bash ci/build.sh --target reflector --preset ezurio_bl54l15u_peri_a2_4_ipt

# Flash the existing ezurio reflector IPT build.
flash-ezurio-reflector-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_ezurio_bl54l15u_peri_a2_4_ipt

# Build and flash ezurio reflector IPT firmware.
build-flash-ezurio-reflector-ipt: build-ezurio-reflector-ipt
    @just flash-ezurio-reflector-ipt

# Build ublox initiator RAS firmware.
build-ublox-initiator:
    bash ci/build.sh --target initiator --preset ublox_cent_a1_4

# Flash the existing ublox initiator RAS build.
flash-ublox-initiator:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_ublox_cent_a1_4

# Build and flash ublox initiator RAS firmware.
build-flash-ublox-initiator: build-ublox-initiator
    @just flash-ublox-initiator

# Build ublox initiator IPT firmware.
build-ublox-initiator-ipt:
    bash ci/build.sh --target initiator --preset ublox_cent_a1_4_ipt

# Flash the existing ublox initiator IPT build.
flash-ublox-initiator-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_ublox_cent_a1_4_ipt

# Build and flash ublox initiator IPT firmware.
build-flash-ublox-initiator-ipt: build-ublox-initiator-ipt
    @just flash-ublox-initiator-ipt

# Build ublox reflector RAS firmware.
build-ublox-reflector:
    bash ci/build.sh --target reflector --preset ublox_peri_a1_4

# Flash the existing ublox reflector RAS build.
flash-ublox-reflector:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_ublox_peri_a1_4

# Build and flash ublox reflector RAS firmware.
build-flash-ublox-reflector: build-ublox-reflector
    @just flash-ublox-reflector

# Build ublox reflector IPT firmware.
build-ublox-reflector-ipt:
    bash ci/build.sh --target reflector --preset ublox_peri_a1_4_ipt

# Flash the existing ublox reflector IPT build.
flash-ublox-reflector-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_ublox_peri_a1_4_ipt

# Build and flash ublox reflector IPT firmware.
build-flash-ublox-reflector-ipt: build-ublox-reflector-ipt
    @just flash-ublox-reflector-ipt

# Build nordic-tag initiator RAS firmware.
build-nordic-tag-initiator:
    bash ci/build.sh --target initiator --preset nrf54l15tag_cent_a2_4

# Flash the existing nordic-tag initiator RAS build.
flash-nordic-tag-initiator:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_nrf54l15tag_cent_a2_4

# Build and flash nordic-tag initiator RAS firmware.
build-flash-nordic-tag-initiator: build-nordic-tag-initiator
    @just flash-nordic-tag-initiator

# Build nordic-tag initiator IPT firmware.
build-nordic-tag-initiator-ipt:
    bash ci/build.sh --target initiator --preset nrf54l15tag_cent_a2_4_ipt

# Flash the existing nordic-tag initiator IPT build.
flash-nordic-tag-initiator-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_nrf54l15tag_cent_a2_4_ipt

# Build and flash nordic-tag initiator IPT firmware.
build-flash-nordic-tag-initiator-ipt: build-nordic-tag-initiator-ipt
    @just flash-nordic-tag-initiator-ipt

# Build nordic-tag reflector RAS firmware.
build-nordic-tag-reflector:
    bash ci/build.sh --target reflector --preset nrf54l15tag_peri_a2_4

# Flash the existing nordic-tag reflector RAS build.
flash-nordic-tag-reflector:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_nrf54l15tag_peri_a2_4

# Build and flash nordic-tag reflector RAS firmware.
build-flash-nordic-tag-reflector: build-nordic-tag-reflector
    @just flash-nordic-tag-reflector

# Build nordic-tag reflector IPT firmware.
build-nordic-tag-reflector-ipt:
    bash ci/build.sh --target reflector --preset nrf54l15tag_peri_a2_4_ipt

# Flash the existing nordic-tag reflector IPT build.
flash-nordic-tag-reflector-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_nrf54l15tag_peri_a2_4_ipt

# Build and flash nordic-tag reflector IPT firmware.
build-flash-nordic-tag-reflector-ipt: build-nordic-tag-reflector-ipt
    @just flash-nordic-tag-reflector-ipt

# Build fanstel initiator RAS firmware.
build-fanstel-initiator:
    bash ci/build.sh --target initiator --preset fanstel_bm15c_cent_a1_4

# Flash the existing fanstel initiator RAS build.
flash-fanstel-initiator:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_fanstel_bm15c_cent_a1_4

# Build and flash fanstel initiator RAS firmware.
build-flash-fanstel-initiator: build-fanstel-initiator
    @just flash-fanstel-initiator

# Build fanstel initiator IPT firmware.
build-fanstel-initiator-ipt:
    bash ci/build.sh --target initiator --preset fanstel_bm15c_cent_a1_4_ipt

# Flash the existing fanstel initiator IPT build.
flash-fanstel-initiator-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_fanstel_bm15c_cent_a1_4_ipt

# Build and flash fanstel initiator IPT firmware.
build-flash-fanstel-initiator-ipt: build-fanstel-initiator-ipt
    @just flash-fanstel-initiator-ipt

# Build fanstel reflector RAS firmware.
build-fanstel-reflector:
    bash ci/build.sh --target reflector --preset fanstel_bm15c_peri_a1_4

# Flash the existing fanstel reflector RAS build.
flash-fanstel-reflector:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_fanstel_bm15c_peri_a1_4

# Build and flash fanstel reflector RAS firmware.
build-flash-fanstel-reflector: build-fanstel-reflector
    @just flash-fanstel-reflector

# Build fanstel reflector IPT firmware.
build-fanstel-reflector-ipt:
    bash ci/build.sh --target reflector --preset fanstel_bm15c_peri_a1_4_ipt

# Flash the existing fanstel reflector IPT build.
flash-fanstel-reflector-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_fanstel_bm15c_peri_a1_4_ipt

# Build and flash fanstel reflector IPT firmware.
build-flash-fanstel-reflector-ipt: build-fanstel-reflector-ipt
    @just flash-fanstel-reflector-ipt

# Build minew initiator RAS firmware.
build-minew-initiator:
    bash ci/build.sh --target initiator --preset minew_me54be01_cent_a1_4

# Flash the existing minew initiator RAS build.
flash-minew-initiator:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_minew_me54be01_cent_a1_4

# Build and flash minew initiator RAS firmware.
build-flash-minew-initiator: build-minew-initiator
    @just flash-minew-initiator

# Build minew initiator IPT firmware.
build-minew-initiator-ipt:
    bash ci/build.sh --target initiator --preset minew_me54be01_cent_a1_4_ipt

# Flash the existing minew initiator IPT build.
flash-minew-initiator-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_minew_me54be01_cent_a1_4_ipt

# Build and flash minew initiator IPT firmware.
build-flash-minew-initiator-ipt: build-minew-initiator-ipt
    @just flash-minew-initiator-ipt

# Build minew reflector RAS firmware.
build-minew-reflector:
    bash ci/build.sh --target reflector --preset minew_me54be01_peri_a1_4

# Flash the existing minew reflector RAS build.
flash-minew-reflector:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_minew_me54be01_peri_a1_4

# Build and flash minew reflector RAS firmware.
build-flash-minew-reflector: build-minew-reflector
    @just flash-minew-reflector

# Build minew reflector IPT firmware.
build-minew-reflector-ipt:
    bash ci/build.sh --target reflector --preset minew_me54be01_peri_a1_4_ipt

# Flash the existing minew reflector IPT build.
flash-minew-reflector-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_minew_me54be01_peri_a1_4_ipt

# Build and flash minew reflector IPT firmware.
build-flash-minew-reflector-ipt: build-minew-reflector-ipt
    @just flash-minew-reflector-ipt

# Build raytac initiator RAS firmware.
build-raytac-initiator:
    bash ci/build.sh --target initiator --preset raytac_an54lv_k15_cent_a3_4

# Flash the existing raytac initiator RAS build.
flash-raytac-initiator:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_raytac_an54lv_k15_cent_a3_4

# Build and flash raytac initiator RAS firmware.
build-flash-raytac-initiator: build-raytac-initiator
    @just flash-raytac-initiator

# Build raytac initiator IPT firmware.
build-raytac-initiator-ipt:
    bash ci/build.sh --target initiator --preset raytac_an54lv_k15_cent_a3_4_ipt

# Flash the existing raytac initiator IPT build.
flash-raytac-initiator-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_raytac_an54lv_k15_cent_a3_4_ipt

# Build and flash raytac initiator IPT firmware.
build-flash-raytac-initiator-ipt: build-raytac-initiator-ipt
    @just flash-raytac-initiator-ipt

# Build raytac reflector RAS firmware.
build-raytac-reflector:
    bash ci/build.sh --target reflector --preset raytac_an54lv_k15_peri_a3_4

# Flash the existing raytac reflector RAS build.
flash-raytac-reflector:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_raytac_an54lv_k15_peri_a3_4

# Build and flash raytac reflector RAS firmware.
build-flash-raytac-reflector: build-raytac-reflector
    @just flash-raytac-reflector

# Build raytac reflector IPT firmware.
build-raytac-reflector-ipt:
    bash ci/build.sh --target reflector --preset raytac_an54lv_k15_peri_a3_4_ipt

# Flash the existing raytac reflector IPT build.
flash-raytac-reflector-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_raytac_an54lv_k15_peri_a3_4_ipt

# Build and flash raytac reflector IPT firmware.
build-flash-raytac-reflector-ipt: build-raytac-reflector-ipt
    @just flash-raytac-reflector-ipt

# Build insight initiator RAS firmware.
build-insight-initiator:
    bash ci/build.sh --target initiator --preset insight_isp2454_cent_a1_4

# Flash the existing insight initiator RAS build.
flash-insight-initiator:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_insight_isp2454_cent_a1_4

# Build and flash insight initiator RAS firmware.
build-flash-insight-initiator: build-insight-initiator
    @just flash-insight-initiator

# Build insight initiator IPT firmware.
build-insight-initiator-ipt:
    bash ci/build.sh --target initiator --preset insight_isp2454_cent_a1_4_ipt

# Flash the existing insight initiator IPT build.
flash-insight-initiator-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_insight_isp2454_cent_a1_4_ipt

# Build and flash insight initiator IPT firmware.
build-flash-insight-initiator-ipt: build-insight-initiator-ipt
    @just flash-insight-initiator-ipt

# Build insight reflector RAS firmware.
build-insight-reflector:
    bash ci/build.sh --target reflector --preset insight_isp2454_peri_a1_4

# Flash the existing insight reflector RAS build.
flash-insight-reflector:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_insight_isp2454_peri_a1_4

# Build and flash insight reflector RAS firmware.
build-flash-insight-reflector: build-insight-reflector
    @just flash-insight-reflector

# Build insight reflector IPT firmware.
build-insight-reflector-ipt:
    bash ci/build.sh --target reflector --preset insight_isp2454_peri_a1_4_ipt

# Flash the existing insight reflector IPT build.
flash-insight-reflector-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_insight_isp2454_peri_a1_4_ipt

# Build and flash insight reflector IPT firmware.
build-flash-insight-reflector-ipt: build-insight-reflector-ipt
    @just flash-insight-reflector-ipt

# Build ophelia initiator RAS firmware.
build-ophelia-initiator:
    bash ci/build.sh --target initiator --preset ophelia4ev_cent_a1_4

# Flash the existing ophelia initiator RAS build.
flash-ophelia-initiator:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_ophelia4ev_cent_a1_4

# Build and flash ophelia initiator RAS firmware.
build-flash-ophelia-initiator: build-ophelia-initiator
    @just flash-ophelia-initiator

# Build ophelia initiator IPT firmware.
build-ophelia-initiator-ipt:
    bash ci/build.sh --target initiator --preset ophelia4ev_cent_a1_4_ipt

# Flash the existing ophelia initiator IPT build.
flash-ophelia-initiator-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_ophelia4ev_cent_a1_4_ipt

# Build and flash ophelia initiator IPT firmware.
build-flash-ophelia-initiator-ipt: build-ophelia-initiator-ipt
    @just flash-ophelia-initiator-ipt

# Build ophelia reflector RAS firmware.
build-ophelia-reflector:
    bash ci/build.sh --target reflector --preset ophelia4ev_peri_a1_4

# Flash the existing ophelia reflector RAS build.
flash-ophelia-reflector:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_ophelia4ev_peri_a1_4

# Build and flash ophelia reflector RAS firmware.
build-flash-ophelia-reflector: build-ophelia-reflector
    @just flash-ophelia-reflector

# Build ophelia reflector IPT firmware.
build-ophelia-reflector-ipt:
    bash ci/build.sh --target reflector --preset ophelia4ev_peri_a1_4_ipt

# Flash the existing ophelia reflector IPT build.
flash-ophelia-reflector-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_ophelia4ev_peri_a1_4_ipt

# Build and flash ophelia reflector IPT firmware.
build-flash-ophelia-reflector-ipt: build-ophelia-reflector-ipt
    @just flash-ophelia-reflector-ipt

# Build kaga initiator RAS firmware.
build-kaga-initiator:
    bash ci/build.sh --target initiator --preset kaga_ec4l15ba1_cent_a1_4

# Flash the existing kaga initiator RAS build.
flash-kaga-initiator:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_kaga_ec4l15ba1_cent_a1_4

# Build and flash kaga initiator RAS firmware.
build-flash-kaga-initiator: build-kaga-initiator
    @just flash-kaga-initiator

# Build kaga initiator IPT firmware.
build-kaga-initiator-ipt:
    bash ci/build.sh --target initiator --preset kaga_ec4l15ba1_cent_a1_4_ipt

# Flash the existing kaga initiator IPT build.
flash-kaga-initiator-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d initiator/build_kaga_ec4l15ba1_cent_a1_4_ipt

# Build and flash kaga initiator IPT firmware.
build-flash-kaga-initiator-ipt: build-kaga-initiator-ipt
    @just flash-kaga-initiator-ipt

# Build kaga reflector RAS firmware.
build-kaga-reflector:
    bash ci/build.sh --target reflector --preset kaga_ec4l15ba1_peri_a1_4

# Flash the existing kaga reflector RAS build.
flash-kaga-reflector:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_kaga_ec4l15ba1_peri_a1_4

# Build and flash kaga reflector RAS firmware.
build-flash-kaga-reflector: build-kaga-reflector
    @just flash-kaga-reflector

# Build kaga reflector IPT firmware.
build-kaga-reflector-ipt:
    bash ci/build.sh --target reflector --preset kaga_ec4l15ba1_peri_a1_4_ipt

# Flash the existing kaga reflector IPT build.
flash-kaga-reflector-ipt:
    west flash --runner jlink --reset --pre-script-cmd "exec DisableAutoUpdateFW" -d reflector/build_kaga_ec4l15ba1_peri_a1_4_ipt

# Build and flash kaga reflector IPT firmware.
build-flash-kaga-reflector-ipt: build-kaga-reflector-ipt
    @just flash-kaga-reflector-ipt
