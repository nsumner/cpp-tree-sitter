// A fixture that returns a language whose ABI below the supported range to
// test the loader's AbiIncompatible path.
//
// Steal the parser.h from the ctstest fixture.
#include "../../ctstest/src/tree_sitter/parser.h"

#define TS_PUBLIC __attribute__((visibility("default")))

// Only abi_version should be read because the loader rejects this before
// touching anything else. Must be below
// TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION.
static const TSLanguage language = {
  .abi_version = 12,
};

TS_PUBLIC const TSLanguage *tree_sitter_oldabi(void) {
  return &language;
}
