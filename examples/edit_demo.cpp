// Incremental parsing. Reparsing after a small edit without redoing the
// whole file.
//
// First tell the old tree what changed using Tree::edit. Then hand that tree
// back to Parser::parse along with the new source. tree-sitter reuses every
// subtree the edit did not touch.
//
// A ts::InputEdit holds three ts::Locations, and each Location pairs a byte
// offset with a Point holding a row and a column.
//
//   start   where the replaced region begins, in the old coordinates
//   oldEnd  where that region ended before the edit, in the old coordinates
//   newEnd  where the replacement ends after the edit, in the new coordinates
//
// Tree::edit shifts the old tree's positions in place, so any Node you
// fetched from that tree beforehand is wrong once the edit is applied. Fetch
// fresh nodes from the tree the reparse hands back.
//
// oldTree.getChangedRanges(newTree) reports differences in structure. It
// compares node types and extents once those extents have been shifted for
// the edit, and it does not compare the text inside a leaf. Both edits below
// rewrite the same one-character value, but only the second one changes a
// node's type, and only the second one is reported. So this tells you
// whether the shape of the tree changed somewhere as opposed to the content
// of leaves.

#include <cstdlib>
#include <print>
#include <string_view>

#include <cpp-tree-sitter.h>
#include <cts/format.h>

extern "C" TSLanguage* tree_sitter_json();


namespace {

// Applies one edit, reparses incrementally, and reports what tree-sitter
// considers structurally different afterwards.
void
reportEdit(ts::Parser& parser, std::string_view label,
           std::string_view oldSource, std::string_view newSource,
           const ts::InputEdit& edit) {
  auto oldTree = parser.parse(oldSource);
  if (!oldTree) {
    std::println(stderr, "error: {}", oldTree.error());
    return;
  }

  oldTree->edit(edit);

  // Passing the edited tree makes this incremental.
  auto newTree = parser.parse(newSource, *oldTree);
  if (!newTree) {
    std::println(stderr, "error: {}", newTree.error());
    return;
  }

  std::println("{}: {} -> {}", label, oldSource, newSource);
  std::println("  tree: {}", newTree->getRootNode().getSExpr());

  auto changed = oldTree->getChangedRanges(*newTree);
  if (changed.empty()) {
    std::println("  no structural change");
  }
  for (const ts::Range& range : changed) {
    std::println("  changed: {}", range);
  }
}

}


int
main() {
  auto parser = ts::Parser::create(tree_sitter_json());
  if (!parser) {
    std::println(stderr, "error: {}", parser.error());
    return EXIT_FAILURE;
  }

  //                                  0123456 7
  constexpr std::string_view before = R"({"a": 1})";

  // Replacing `1` with `42` leaves a `number` where a `number` was, so the
  // shape of the tree is unchanged and nothing is reported.
  reportEdit(*parser, "same node type", before, R"({"a": 42})",
             ts::InputEdit{
               .start  = {.byte=6, .point={0, 6}},
               .oldEnd = {.byte=7, .point={0, 7}},
               .newEnd = {.byte=8, .point={0, 8}},
             });

  // Replacing `1` with `true` swaps a `number` for a `true`. That is a
  // change in shape, so this one is reported.
  reportEdit(*parser, "new node type ", before, R"({"a": true})",
             ts::InputEdit{
               .start  = {.byte=6, .point={0, 6}},
               .oldEnd = {.byte=7, .point={0, 7}},
               .newEnd = {.byte=10, .point={0, 10}},
             });
  return EXIT_SUCCESS;
}
