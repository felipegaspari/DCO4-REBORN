#!/usr/bin/env bash
# Compile INPUT-CONTROLLER for this project (DCO4-REBORN).
#
# INPUT-CONTROLLER is a submodule shared byte-for-byte with DCO3-MONOSYNTH. It
# picks the instrument up from ../project_config.h through a symlink, so this
# script no longer supplies a model override: the command below is identical to
# the one DCO3-MONOSYNTH uses. Only the ROXMUX flag remains, and that is a
# performance choice (the library's SRAM hot path collides with Rox74HC595's
# section attribute), not an instrument one.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

arduino-cli compile \
  --fqbn rp2040:rp2040:rpipico \
  --libraries ./INPUT-CONTROLLER/_build_libs \
  --build-property "compiler.cpp.extra_flags=-DROXMUX_FELA_SRAM_HOT=0" \
  ./INPUT-CONTROLLER "$@"
