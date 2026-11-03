#!/usr/bin/env python3
"""Run ARM parser regression tests with the SDK from an existing RAS build.

Usage: python3 tests/mode1/run.py initiator/build_<preset>/initiator
Requires the build's compile_commands.json, ARM GCC, and qemu-system-arm.
Uses the SDK's actual RAS parser and net-buffer implementation.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("build", type=Path)
    options = cli.parse_args()
    repo = Path(__file__).resolve().parents[2]
    build = options.build.resolve()
    entries = json.loads((build / "compile_commands.json").read_text())
    entry = next(e for e in entries if e["file"].endswith("/common/cs_step_parse.c"))
    args = shlex.split(entry["command"])
    index = args.index("-o")
    del args[index:index + 2]
    args.remove("-c")
    args.remove(entry["file"])
    args = [a for a in args if not a.startswith("-specs=")]
    sdk = next(Path(a[2:]).parent for a in args if a.startswith("-I") and a.endswith("/zephyr/include"))
    ncs = sdk.parent
    tests = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="cs-subevent-tests-") as directory:
        out = Path(directory)
        index = args.index("-imacros")
        original_config = Path(args[index + 1])
        config = "\n".join(line for line in original_config.read_text().splitlines()
                           if not line.startswith(("#define CONFIG_LOG", "#define CONFIG_ASSERT")))
        shadow = out / "include/zephyr"
        shadow.mkdir(parents=True)
        (shadow / "autoconf.h").write_text(config + "\n")
        args[index + 1] = str(shadow / "autoconf.h")
        args[1:1] = ["-I" + str(out / "include"), "-I" + str(repo / "common")]
        # Match the MPS2 AN386's Cortex-M4, including atomic instructions.
        # Production target compilation is checked separately by the firmware build.
        args += ["-mcpu=cortex-m4", "-mfpu=fpv4-sp-d16"]
        parser = (ncs / "nrf/subsys/bluetooth/services/ras/rreq/ras_rreq.c").read_text()
        parser = parser[parser.index("void bt_ras_rreq_rd_subevent_data_parse("):]
        source = ('#include "subevent.h"\n#include "cs_initiator.h"\n'
                  '#include <bluetooth/services/ras.h>\n'
                  '#define LOG_ERR(...) ((void)0)\n#define LOG_WRN(...) ((void)0)\n'
                  '#define LOG_DBG(...) ((void)0)\n#define LOG_INF(...) ((void)0)\n')
        source += parser + "\n" + (tests / "fixtures.c").read_text()
        (out / "test.c").write_text(source)
        objects = []
        for src in [repo / "common/cs_step_parse.c", repo / "common/subevent.c",
                    sdk / "lib/net_buf/buf_simple.c", out / "test.c"]:
            obj = out / (src.stem + ".o")
            subprocess.run(args + ["-c", str(src), "-o", str(obj)],
                           cwd=entry["directory"], check=True)
            objects.append(str(obj))
        subprocess.run([args[0], "-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16",
                        "-mfloat-abi=hard", "-nostartfiles", "-Wl,--gc-sections",
                        "-T" + str(tests / "test.ld"), *objects, "-lc", "-lgcc", "-lnosys",
                        "-o", str(out / "test.elf")], check=True)
        subprocess.run(["qemu-system-arm", "-M", "mps2-an386", "-nographic",
                        "-semihosting", "-kernel", str(out / "test.elf")],
                       timeout=15, check=True)



if __name__ == "__main__":
    main()
