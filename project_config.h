#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

// -----------------------------------------------------------------------------
// Which instrument this superproject is.
//
// INPUT-CONTROLLER, SCREEN-CONTROLLER and DCO-CONTROL-PANEL are one repo each,
// checked out into both DCO3-MONOSYNTH and DCO4-REBORN. They read this file
// through a symlink that resolves to whichever project they are sitting in, so
// their sources stay byte-identical in both trees and nothing has to be chosen
// at build time: no flag, no build script, no per-checkout edit.
//
//   3 = DCO3-MONOSYNTH, 1 voice, 3 oscillators + sub
//   4 = DCO4-REBORN, 4 voices of 2 oscillators
//
// The guard leaves -DPROJECT_INSTRUMENT=3 working, to compile-check the other
// instrument from this tree without touching a file.
// -----------------------------------------------------------------------------

#ifndef PROJECT_INSTRUMENT
#define PROJECT_INSTRUMENT 4
#endif

#endif  // PROJECT_CONFIG_H
