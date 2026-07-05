// Loads a grammar that is already installed. e.g. for Neovim, rather than
// building one from source.

#include <cstdlib>
#include <print>
#include <string_view>

#include <cpp-tree-sitter.h>
#include <cts/loader.h>
#include <cts/loader/format.h>

int
main(int argc, char** argv) {
  std::string_view const languageName = argc > 1 ? argv[1] : "json";

  // The preset is a plain vector of candidates. Nothing is searched that we
  // did not ask for, and adding a plugin manager's directory can be line:
  //   directories.emplace_back("/home/me/.local/share/nvim/lazy/nvim-treesitter/parser");
  auto directories = ts::layout::nvimTreesitter();
  ts::GrammarSearchPath const grammars{std::move(directories)};

  auto language = grammars.load(languageName);
  if (!language) {
    std::println("could not load {}: {}", languageName, language.error());
    std::println("installed: {}", grammars.available().size());
    return EXIT_FAILURE;
  }

  auto parser = ts::Parser::create(*language);
  if (!parser) {
    std::println("could not create a parser: {}", parser.error());
    return EXIT_FAILURE;
  }

  // languageName was "json" by default, which should parse the source below.
  // Naming a different one on the command line loads that grammar and parses
  // this text with it, which likely fails with other languages.
  auto tree = parser->parse(R"({"answer": 42})");
  if (!tree) {
    std::println("could not parse: {}", tree.error());
    return EXIT_FAILURE;
  }

  std::println("{}", tree->getRootNode().getSExpr());
  return EXIT_SUCCESS;
}
