import os
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import subprocess
import sys

try:
    debug = int(os.environ.get("SUPERCAN_DEBUG", "0")) != 0
except:
    debug = False

class BuildExt(build_ext):
    def build_extensions(self):
        for ext in self.extensions:
            if self.compiler.compiler_type == "msvc":
                ext.extra_compile_args.append("/std:c++20")
                if debug:
                    ext.extra_compile_args.extend(["/Od", "/Zi"])
                    ext.extra_link_args.append("/DEBUG")
        build_ext.build_extensions(self)


def main():
    global debug

    try:
        commit = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).decode('ascii').strip()
    except:
        commit = "<unknown>"

    try:
        with open("../dll/commit.h", "x") as f:  # 'x' enables you to place your own file
            f.write(f"#define SC_COMMIT \"{commit}\"\n")
    except FileExistsError:
        pass

    if "--sc-debug-build" in sys.argv:
        debug = True
        sys.argv.remove("--sc-debug-build")

    setup(
        name="python-can-supercan",
        version="0.1.0",
        description="Python interface for SuperCAN",
        author="Jean Gressmann",
        author_email="jean@0x42.de",
        download_url="https://github.com/jgressmann/supercan/",
        install_requires=["python-can >= 4.5.0"],
        ext_modules=[
            Extension(
                name="supercan",
                sources=["module.cpp", "../dll/supercan_dll.c", "../../src/can_bit_timing.c"],
                include_dirs=["../inc", "../../src"],
                define_macros=[("SC_STATIC", "1")],
                undef_macros=["NDEBUG"] if debug else [],
                libraries=["winusb", "Cfgmgr32", "Ole32"],
            )
        ],
        cmdclass={"build_ext": BuildExt},
        entry_points = {
            "can.interface": [
                "supercan-exclusive = supercan:Exclusive"
            ]
        },
        license="MIT",
        platforms=["Windows"],
    )

if __name__ == "__main__":
    main()
