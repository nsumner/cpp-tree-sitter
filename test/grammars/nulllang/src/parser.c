// A fixture that exports a well-formed entry point with no language to test
// the loader's LanguageNull path.

#include <stddef.h>

// Generated parsers annotate their entry point to survive being built into a
// shared object.
#define TS_PUBLIC __attribute__((visibility("default")))

typedef struct TSLanguage TSLanguage;

TS_PUBLIC const TSLanguage *tree_sitter_nulllang(void) {
  return NULL;
}
