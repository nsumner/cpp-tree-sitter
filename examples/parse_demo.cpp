#include <cstdlib>
#include <print>
#include <string_view>

#include <cpp-tree-sitter.h>
#include <cts/format.h>

extern "C" TSLanguage* tree_sitter_json();

int
main() {
  auto parser = ts::Parser::create(tree_sitter_json());
  if (!parser) {
    std::println(stderr, "error: {}", parser.error());
    return EXIT_FAILURE;
  }

  std::string_view constexpr sourcecode = "[1, null]";
  auto const tree = parser->parse(sourcecode);
  if (!tree) {
    std::println(stderr, "error: {}", tree.error());
    return EXIT_FAILURE;
  }

  ts::Node const root = tree->getRootNode();
  std::println("root: {}", root.getType());

  for (ts::Node const child : root.getNamedChild(0)->getNamedChildren()) {
    std::println("  {}: '{}'", child, child.getSourceRange(sourcecode));
  }

  std::println("Syntax tree: {}", root.getSExpr());
  return EXIT_SUCCESS;
}
