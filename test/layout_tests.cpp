#include <algorithm>
#include <filesystem>
#include <string>

#include <doctest/doctest.h>
#include <cts/loader.h>

namespace {

ts::layout::LayoutEnvironment
homeOnly() {
  ts::layout::LayoutEnvironment environment;
  environment.home = std::filesystem::path{"/home/tester"};
  return environment;
}


bool
contains(std::vector<std::filesystem::path> const& paths,
         std::filesystem::path const& wanted) {
  return std::ranges::find(paths, wanted) != paths.end();
}

}  // namespace


TEST_CASE("The nvim preset covers the XDG defaults and the distro paths") {
  auto const directories = ts::layout::nvimTreesitterUnder(homeOnly());

  CHECK(contains(directories, "/home/tester/.config/nvim/parser"));
  CHECK(contains(directories, "/home/tester/.local/share/nvim/site/parser"));
  CHECK(contains(directories, "/usr/share/nvim/site/parser"));
  CHECK(contains(directories, "/usr/lib/nvim/parser"));
}


TEST_CASE("An explicit XDG variable overrides the home-relative default") {
  auto environment = homeOnly();
  environment.dataHome = "/xdg/data";

  auto const directories = ts::layout::nvimTreesitterUnder(environment);

  CHECK(contains(directories, "/xdg/data/nvim/site/parser"));
  CHECK(!contains(directories, "/home/tester/.local/share/nvim/site/parser"));
}


// XDG says a variable that is set but empty means "use the default".
TEST_CASE("An empty variable falls back to the default") {
  auto environment = homeOnly();
  environment.dataHome = "";

  auto const directories = ts::layout::nvimTreesitterUnder(environment);

  CHECK(contains(directories, "/home/tester/.local/share/nvim/site/parser"));
}


// An omitted variable must drop its entry. Keeping it would yield a relative
// path resolved against the working directory instead.
TEST_CASE("An entry whose variable is absent is omitted rather than made relative") {
  ts::layout::LayoutEnvironment const empty;

  auto const directories = ts::layout::nvimTreesitterUnder(empty);

  for (auto const& directory : directories) {
    CHECK(directory.is_absolute());
  }
  CHECK(!contains(directories, ".config/nvim/parser"));
}

TEST_CASE("Every preset returns only absolute paths") {
  auto environment = homeOnly();
  environment.dataDirs = "/abs/one:relative/two:";
  environment.vimRuntime = std::filesystem::path{"/usr/share/nvim/runtime"};
  environment.helixRuntime = std::filesystem::path{"/opt/helix/runtime"};
  environment.cacheHome = "/xdg/cache";

  for (auto const& preset : {ts::layout::nvimTreesitterUnder(environment),
                             ts::layout::helixUnder(environment),
                             ts::layout::treeSitterCliUnder(environment)}) {
    // An empty result satisfies "every path is absolute" vacuously.
    CHECK(!preset.empty());
    for (auto const& directory : preset) {
      CHECK(directory.is_absolute());
    }
  }
}


TEST_CASE("Duplicate entries collapse while keeping their order") {
  auto environment = homeOnly();
  // The data home commonly also appears in the data dirs list.
  environment.dataHome = "/usr/share";
  environment.dataDirs = "/usr/share:/usr/share";

  auto const directories = ts::layout::nvimTreesitterUnder(environment);

  auto const occurrences = std::ranges::count(
    directories, std::filesystem::path{"/usr/share/nvim/site/parser"});
  CHECK(occurrences == 1);
}


TEST_CASE("The helix preset prefers an explicit runtime") {
  auto environment = homeOnly();
  environment.helixRuntime = std::filesystem::path{"/opt/helix/runtime"};

  auto const directories = ts::layout::helixUnder(environment);

  CHECK(directories.front() == "/opt/helix/runtime/grammars");
  CHECK(contains(directories, "/home/tester/.config/helix/runtime/grammars"));
}


TEST_CASE("The tree-sitter CLI preset uses the cache directory") {
  CHECK(contains(ts::layout::treeSitterCliUnder(homeOnly()),
                 "/home/tester/.cache/tree-sitter/lib"));
}


// These results depend on the machine, so the pure overloads above carry the
// real assertions and this only checks that the entry points run.
TEST_CASE("The environment-reading presets are callable") {
  CHECK_NOTHROW((void)ts::layout::nvimTreesitter());
  CHECK_NOTHROW((void)ts::layout::helix());
  CHECK_NOTHROW((void)ts::layout::treeSitterCli());
}
