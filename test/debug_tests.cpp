#include <cstdio>
#include <string>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>

extern "C" TSLanguage* tree_sitter_json();


TEST_CASE("printDotGraph emits a DOT description of the tree") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  const auto tree = parser.parse("[1]").value();

  std::FILE* file = std::tmpfile();
  REQUIRE(file != nullptr);
  tree.printDotGraph(file);
  std::fflush(file);

  std::rewind(file);
  char buffer[64] = {};
  REQUIRE(std::fgets(buffer, sizeof(buffer), file) != nullptr);
  CHECK(std::string{buffer}.find("digraph") != std::string::npos);
  std::fclose(file);
}


TEST_CASE("printDotGraph with a null file is a safe no-op") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  const auto tree = parser.parse("[1]").value();

  tree.printDotGraph(nullptr);

  CHECK(tree.getRootNode().getType() == "document");
}


TEST_CASE("printDotGraphs streams parser activity during a parse") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  std::FILE* file = std::tmpfile();
  REQUIRE(file != nullptr);

  parser.printDotGraphs(file);
  const auto tree = parser.parse("[1]");
  REQUIRE(tree.has_value());
  // Disabling closes tree-sitter's dup'd stream, flushing it.
  parser.printDotGraphs(nullptr);

  std::rewind(file);
  char buffer[64] = {};
  REQUIRE(std::fgets(buffer, sizeof(buffer), file) != nullptr);
  CHECK(std::string{buffer}.find("graph") != std::string::npos);
  std::fclose(file);
}


TEST_CASE("printDotGraphs with a null file is a safe no-op") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  parser.printDotGraphs(nullptr);
  CHECK(parser.parse("[1]").has_value());
}
