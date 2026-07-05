# ctstest fixture grammar

A tiny grammar to exercise specific grammar constructs, supertype inspection,
and other newer features that emerge. This grammar provides:
* nested supertypes (`_numeric` inside `_literal`)
* incomparable supertype overlap (`_literal` and `_stringy` both contain `string`)
* a supertype diamond (`_numeric` inside both `_literal` and `_scalar`, which are incomparable)
* a true alias symbol (`key`)
* a renamed anonymous token (`minus`)

`src/` is generated output committed to be self contained.
To regenerate after editing `grammar.js`:

    cd test/grammars/ctstest && tree-sitter generate

Requires tree-sitter CLI >= 0.25. `tree-sitter.json` must be present or the
CLI may emit ABI 14. Verify `#define LANGUAGE_VERSION 15` in the regenerated
`src/parser.c`.
