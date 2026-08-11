#!/usr/bin/env bash
# Compile INPUT-CONTROLLER for this project (DCO4-REBORN).
#
# INPUT-CONTROLLER is a submodule shared byte-for-byte with DCO3-MONOSYNTH;
# its board_model.h defaults to DCO3. This script is the one place that
# supplies the DCO4 override, so nobody has to remember (or hand-edit the
# submodule) to get the right voice count and UART wiring.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

arduino-cli compile \
  --fqbn rp2040:rp2040:rpipico \
  --libraries ./INPUT-CONTROLLER/_build_libs \
  --build-property "compiler.cpp.extra_flags=-DINPUT_BOARD_MODEL=INPUT_BOARD_DCO4 -DROXMUX_FELA_SRAM_HOT=0" \
  ./INPUT-CONTROLLER "$@"
