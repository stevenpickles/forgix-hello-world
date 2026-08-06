# Machine-specific tool locations for the Forgix build scripts.
#
# Copy this file to scripts/env.local.sh (gitignored) and uncomment what your
# machine provides. scripts/env.sh sources env.local.sh before applying its own
# fallbacks. Keep the `: "${VAR:=...}"` form: it lets anything already exported
# in the environment win, which is the contract every script relies on.
#
# The values shown are the installation layout the project was brought up with;
# docs/picotool-windows.md records how that layout was established and verified.

# Core toolchains -- ./scripts/bootstrap.sh reports on all of these.
# : "${EFINITY_HOME:=/c/Efinix/Efinity/2026.1}"
# : "${PICO_SDK_PATH:=/c/RPi/pico-sdk-2.3.0}"
# : "${GHDL_BIN_PATH:=/c/Forgix/GHDL/ghdl-mcode-6.0.0-ucrt64/bin}"

# The USB-enabled picotool install produced by ./scripts/build_picotool.sh.
# : "${PICOTOOL_BIN_PATH:=/c/RPi/picotool-2.3.0-install-usb/picotool}"

# Formatters, when pip's user-site or the LLVM installer left them off PATH.
# : "${VSG:=/c/Users/<you>/AppData/Roaming/Python/<version>/Scripts/vsg}"
# : "${CLANG_FORMAT:=/c/Program Files/LLVM/bin/clang-format.exe}"

# Inputs to ./scripts/build_picotool.sh. The build and install trees default to
# <PICOTOOL_SOURCE>-build-usb and <PICOTOOL_SOURCE>-install-usb.
# : "${PICOTOOL_SOURCE:=/c/RPi/picotool-2.3.0}"
# : "${LIBUSB_ROOT:=/c/Forgix/libusb-1.0.29}"
# : "${CMAKE_EXE:=/c/ST/STM32CubeCLT_1.20.0/CMake/bin/cmake.exe}"
