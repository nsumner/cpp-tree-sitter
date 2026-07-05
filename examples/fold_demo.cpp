// Folding a tree into a value with ts::fold.
//
// ts::fold computes a value from the bottom up. Each node's result is built
// from the results already computed for its children, and the engine handles
// the underlying bookkeeping so you can just write:
//     onNode(node, childResults)
// Leaves get an empty span of results because they have no chldren. The
// result type of the fold is explicit at the call site because nothing in
// the folder's signature has to name it:
//     ts::fold<int>(...)
//
// Unlike ts::visit, a fold allocates because the engine holds a stack of
// frames containing the child results.
//
// Building your own objects out of a parse tree, a typed AST for example, has
// three reasonable shapes.
//
//   * Recursive descent over ts::Node, using field accessors, optional
//     navigation, and children views by hand. Try this first for an AST
//     shaped like the grammar, since each recursive call can return the type
//     its production implies.
//   * ts::fold when a node's value is a straightforward combination of its
//     children's values.
//   * A visitor with a stack of partial values, when construction is naturally
//     bracketed. Push a partial value in onEnter, then pop it in onLeave and
//     attach it to its parent.
//
// ts::typedFold, in typed_demo.cpp, adds dispatch by node type.

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <print>
#include <span>
#include <string_view>

#include <cpp-tree-sitter.h>
#include <cts/format.h>

extern "C" TSLanguage* tree_sitter_json();


namespace {

// Leaf-driven. Every `number` contributes its own value.
// Every other node just passes its children's total upwards.
struct SumNumbers {
  std::string_view source;

  int onNode(ts::Node node, std::span<int> childResults) {
    int total = 0;
    for (int const value : childResults) {
      total += value;
    }
    if (node.getType() == "number") {
      // from_chars parses straight out of the source buffer and leaves
      // `value` untouched if the text does not scan as an int.
      std::string_view const text = node.getSourceRange(source);
      int value = 0;
      std::from_chars(text.data(), text.data() + text.size(), value);
      total += value;
    }
    return total;
  }
};


// Children-driven. A node's answer depends only on its children's answers.
// Leaves get an empty span, so they fall out as depth 1.
struct MaxDepth {
  int onNode(ts::Node node, std::span<int> childResults) {
    int deepest = 0;
    for (int const depth : childResults) {
      deepest = std::max(deepest, depth);
    }
    return node.isNamed() ? deepest + 1 : deepest;
  }
};

}


int
main() {
  auto parser = ts::Parser::create(tree_sitter_json());
  if (!parser) {
    std::println(stderr, "error: {}", parser.error());
    return EXIT_FAILURE;
  }

  constexpr std::string_view source = R"({"a": [1, 2, 3], "b": {"c": 4}})";
  auto tree = parser->parse(source);
  if (!tree) {
    std::println(stderr, "error: {}", tree.error());
    return EXIT_FAILURE;
  }

  ts::Node const root = tree->getRootNode();

  SumNumbers summer{source};
  std::println("sum of every number:  {}", ts::fold<int>(root, summer));

  MaxDepth depth;
  std::println("deepest named nesting: {}", ts::fold<int>(root, depth));
  return EXIT_SUCCESS;
}
