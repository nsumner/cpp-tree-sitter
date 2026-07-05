#include <cstdlib>
#include <print>
#include <string_view>

#include <cpp-tree-sitter.h>
#include <cts/loader.h>

extern "C" TSLanguage* tree_sitter_json();

int
main() {
  auto parser = ts::Parser::create(tree_sitter_json());
  if (!parser) {
    std::println(stderr, "error: {}", parser.error().message());
    return EXIT_FAILURE;
  }

  constexpr std::string_view sourcecode = "[1, null]";
  auto tree = parser->parse(sourcecode);
  if (!tree) {
    std::println(stderr, "error: {}", tree.error().message());
    return EXIT_FAILURE;
  }

  ts::Node root = tree->getRootNode();
  std::println("root: {}", root.getType());

  for (ts::Node child : root.getNamedChild(0)->getNamedChildren()) {
    std::println("  {}: '{}'", child.getType(), child.getSourceRange(sourcecode));
  }

  std::println("Syntax tree: {}", root.getSExpr());

  // Proves the opt-in target propagates its define, its header, and its
  // link dependency through a real consuming build.
  auto loaded = ts::loadGrammar(CTS_CONSUMER_CTSTEST_MODULE);
  if (!loaded) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
