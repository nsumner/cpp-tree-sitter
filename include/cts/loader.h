#ifndef CTS_LOADER_H
#define CTS_LOADER_H

// Opt-in support for dynamically loadable grammars.


#ifndef CTS_DYNAMIC_GRAMMARS
#  error "<cts/loader.h> requires linking cts::cpp-tree-sitter-dynamic"
#endif

#if defined(_WIN32)
#  error "<cts/loader.h> is POSIX-only: runtime grammar loading uses dlfcn"
#endif

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <dlfcn.h>

#include <cts/language.h>


namespace ts::detail {

// An opaque handle on a library that was dynamically loaded.
struct LibraryHandle {
  void* value = nullptr;
};


inline LibraryHandle
openLibrary(char const* path) noexcept {
  // RTLD_LOCAL keeps two loaded grammars from interposing on each other's
  // symbols. RTLD_NOW surfaces an unresolved symbol here rather than at an
  // arbitrary later call.
  return LibraryHandle{dlopen(path, RTLD_NOW | RTLD_LOCAL)};
}


inline void*
findSymbol(LibraryHandle handle, char const* symbol) noexcept {
  return dlsym(handle.value, symbol);
}


// The loader's error state is cleared when read, so this must be called
// immediately after the failing call. It may yield nothing even after a
// real failure.
inline std::string
takeLibraryError() {
  char const* message = dlerror();
  return message == nullptr ? std::string{} : std::string{message};
}


inline void
clearLibraryError() noexcept {
  dlerror();
}


// Given a library like json.so, create the entrypoint tree-sitter
// heuristically from its name (e.g. tree_sitter_json).
inline std::string
entryPointName(std::filesystem::path const& file) {
  std::string stem = file.stem().string();
  std::ranges::replace(stem, '-', '_');
  return "tree_sitter_" + stem;
}


// nvim-treesitter names parsers <lang>.so even on macOS, where they are
// Mach-O bundles, so try .so first everywhere.
constexpr std::span<std::string_view const>
moduleSuffixes() noexcept {
#if defined(__APPLE__)
  static constexpr std::string_view suffixes[]{".so", ".dylib"};
#else
  static constexpr std::string_view suffixes[]{".so"};
#endif
  return suffixes;
}


// Whether a name refers to a single file directly inside a search directory.
// Rejects paths and special components so callers cannot escape the directories
// they explicitly searched.
[[nodiscard]] inline bool
isSingleFileNameComponent(std::string_view name) noexcept {
  return !name.empty()
      && name.find('/') == std::string_view::npos
      && name != "."
      && name != "..";
}

}  // namespace ts::detail


namespace ts {

// Loading failures are a bit more complicated and must carry owned strings
// with details about the error.
struct GrammarLoadError {
  enum class Kind : uint8_t {
    FileMissing,
    LibraryOpenFailed,
    EntryPointMissing,
    LanguageNull,
    AbiIncompatible,
    NotFoundInSearchPath,
  };

  Kind kind;
  // The file attempted, or the language name for NotFoundInSearchPath.
  std::string path;
  // The OS message, the ABI numbers, or the directories searched.
  std::string detail;


  [[nodiscard]] constexpr std::string_view
  message() const noexcept {
    switch (kind) {
      case Kind::FileMissing:
        return "grammar file does not exist";
      case Kind::LibraryOpenFailed:
        return "grammar library could not be opened";
      case Kind::EntryPointMissing:
        return "grammar library has no such entry point";
      case Kind::LanguageNull:
        return "grammar entry point returned no language";
      case Kind::AbiIncompatible:
        return "grammar ABI version is incompatible with this tree-sitter runtime";
      case Kind::NotFoundInSearchPath:
        return "no grammar for this language in the search path";
    }
    return "unknown grammar load error";
  }
};


[[nodiscard]] constexpr bool
abiIsSupported(uint32_t abiVersion) noexcept {
  return abiVersion >= TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION
      && abiVersion <= TREE_SITTER_LANGUAGE_VERSION;
}


// A strongly typed view for the symbol name of a library's entry point.
struct EntryPoint {
  std::string_view value;
};


// Loads a grammar module and returns its language. The module is never
// unloaded.
[[nodiscard]] inline std::expected<Language, GrammarLoadError>
loadGrammar(std::filesystem::path const& file, EntryPoint entryPoint) {
  std::error_code code;
  if (!std::filesystem::exists(file, code) && !code) {
    return std::unexpected(GrammarLoadError{
      .kind = GrammarLoadError::Kind::FileMissing,
      .path = file.string(),
      .detail = std::string{},
    });
  }

  detail::LibraryHandle const handle = detail::openLibrary(file.c_str());
  if (handle.value == nullptr) {
    return std::unexpected(GrammarLoadError{
      .kind = GrammarLoadError::Kind::LibraryOpenFailed,
      .path = file.string(),
      .detail = detail::takeLibraryError(),
    });
  }

  // A string_view is not guaranteed null-terminated, and the symbol lookup
  // needs a C string.
  std::string const symbol{entryPoint.value};

  // clear stale errors first to avoid being misdirected by prior errors.
  detail::clearLibraryError();
  void* const address = detail::findSymbol(handle, symbol.c_str());
  if (address == nullptr) {
    return std::unexpected(GrammarLoadError{
      .kind = GrammarLoadError::Kind::EntryPointMissing,
      .path = file.string(),
      .detail = symbol + ": " + detail::takeLibraryError(),
    });
  }

  using EntryPointFunction = TSLanguage const* (*)();
  auto const entry = reinterpret_cast<EntryPointFunction>(address);
  TSLanguage const* const language = entry();
  if (language == nullptr) {
    return std::unexpected(GrammarLoadError{
      .kind = GrammarLoadError::Kind::LanguageNull,
      .path = file.string(),
      .detail = symbol,
    });
  }

  uint32_t const abiVersion = ts_language_abi_version(language);
  if (!abiIsSupported(abiVersion)) {
    return std::unexpected(GrammarLoadError{
      .kind = GrammarLoadError::Kind::AbiIncompatible,
      .path = file.string(),
      .detail = "grammar ABI is " + std::to_string(abiVersion)
              + ", but runtime accepts "
              + std::to_string(TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION)
              + "-" + std::to_string(TREE_SITTER_LANGUAGE_VERSION),
    });
  }

  return Language{language};
}


// Loads a grammar assuming that the language name comes from the filename.
[[nodiscard]] inline std::expected<Language, GrammarLoadError>
loadGrammar(std::filesystem::path const& file) {
  std::string const symbol = detail::entryPointName(file);
  return loadGrammar(file, EntryPoint{symbol});
}


// Looks grammars up by name across directories the caller supplies. Nothing
// is searched that the caller did not name.
class GrammarSearchPath {
public:
  explicit GrammarSearchPath(std::vector<std::filesystem::path> directories)
    : searchDirectories{std::move(directories)}
      { }


  // Runs the search for a grammar in the provided locations. The file stem
  // is the grammar name.
  [[nodiscard]] std::optional<std::filesystem::path>
  find(std::string_view language) const {
    if (!detail::isSingleFileNameComponent(language)) {
      return std::nullopt;
    }

    for (auto const& directory : searchDirectories) {
      for (std::string_view const suffix : detail::moduleSuffixes()) {
        std::filesystem::path candidate = directory / language;
        candidate += suffix;

        std::error_code code;
        if (std::filesystem::is_regular_file(candidate, code)) {
          return candidate;
        }
      }
    }
    return std::nullopt;
  }


  [[nodiscard]] std::expected<Language, GrammarLoadError>
  load(std::string_view language) const {
    if (auto const file = find(language)) {
      return loadGrammar(*file);
    }
    return std::unexpected(notFound(language));
  }

  // Load a grammar with an explicit entrypoint
  [[nodiscard]] std::expected<Language, GrammarLoadError>
  load(std::string_view language, EntryPoint entryPoint) const {
    if (auto const file = find(language)) {
      return loadGrammar(*file, entryPoint);
    }
    return std::unexpected(notFound(language));
  }


  // Returns the stems of files that look like grammar modules. This may be
  // useful for callers that adapt to whatever parsers are installed. Files
  // are filtered only by extension, so they may still not be grammars.
  [[nodiscard]] std::vector<std::string>
  available() const {
    std::vector<std::string> names;

    auto const suffixes = detail::moduleSuffixes();
    auto isGrammarCandidate = [&suffixes](auto& entry) {
      auto const& path = entry->path();
      auto const extension = path.extension();
      if (!std::ranges::contains(suffixes, extension)) {
        return false;
      }

      std::error_code fileCode;
      return entry->is_regular_file(fileCode);
    };

    for (auto const& directory : searchDirectories) {
      std::error_code code;
      for (std::filesystem::directory_iterator entry{directory, code}, end;
           !code && entry != end;
           entry.increment(code)) {
        if (isGrammarCandidate(entry)) {
            names.push_back(entry->path().stem().string());
        }
      }
    }

    std::ranges::sort(names);
    auto const duplicates = std::ranges::unique(names);
    names.erase(duplicates.begin(), duplicates.end());
    return names;
  }


  [[nodiscard]] std::span<std::filesystem::path const>
  directories() const noexcept {
    return searchDirectories;
  }

private:
  [[nodiscard]] GrammarLoadError
  notFound(std::string_view language) const {
    auto searched = searchDirectories
      | std::views::transform([](auto const& directory) {
        return directory.string();
      })
      | std::views::join_with(std::string_view{", "})
      | std::ranges::to<std::string>();

    return GrammarLoadError{
      .kind = GrammarLoadError::Kind::NotFoundInSearchPath,
      .path = std::string{language},
      .detail = searched,
    };
  }

  std::vector<std::filesystem::path> searchDirectories;
};


// Candidate directories and helpers for common locations of grammars
// that other tools use.
namespace layout {

struct LayoutEnvironment {
  std::optional<std::filesystem::path> home;
  std::optional<std::string> configHome;
  std::optional<std::string> dataHome;
  std::optional<std::string> cacheHome;
  std::optional<std::string> dataDirs;
  std::optional<std::string> configDirs;
  std::optional<std::filesystem::path> vimRuntime;
  std::optional<std::filesystem::path> helixRuntime;
};


namespace detail {

// XDG treats a variable that is set but empty as unset.
inline bool
isSet(std::optional<std::string> const& value) noexcept {
  return value.has_value() && !value->empty();
}


// Resolves one XDG base directory by consulting a possible environmental
// override and otherwise trying the home directory.
inline std::optional<std::filesystem::path>
baseDirectory(std::optional<std::string> const& variable,
              std::optional<std::filesystem::path> const& home,
              std::string_view homeRelativeDefault) {
  if (isSet(variable)) {
    return std::filesystem::path{*variable};
  }
  if (home) {
    return *home / homeRelativeDefault;
  }
  return std::nullopt;
}


inline std::vector<std::filesystem::path>
splitList(std::optional<std::string> const& variable, std::string_view fallback) {
  std::string_view const list = isSet(variable) ? std::string_view{*variable} : fallback;

  std::vector<std::filesystem::path> entries;
  for (auto const part : std::views::split(list, ':')) {
    std::string_view const text{part.begin(), part.end()};
    if (!text.empty()) {
      entries.emplace_back(text);
    }
  }
  return entries;
}


// The XDG specification requires absolute paths, so relative entries are
// discarded rather than resolved against the working directory.
inline void
appendIfAbsolute(std::vector<std::filesystem::path>& directories,
                 std::filesystem::path candidate) {
  if (candidate.is_absolute()) {
    directories.push_back(std::move(candidate));
  }
}


// Directories might appear multiple times but should not be probed twice.
// Deduplicate them and preserve order to maintain priority.
inline void
deduplicate(std::vector<std::filesystem::path>& directories) {
  std::vector<std::filesystem::path> unique;
  for (auto& directory : directories) {
    if (std::ranges::find(unique, directory) == unique.end()) {
      unique.push_back(std::move(directory));
    }
  }
  directories = std::move(unique);
}


inline std::optional<std::string>
variable(char const* name) {
  char const* const value = std::getenv(name);
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string{value};
}

}  // namespace detail


inline LayoutEnvironment
currentEnvironment() {
  LayoutEnvironment environment;
  if (auto const home = detail::variable("HOME")) {
    environment.home = std::filesystem::path{*home};
  }
  environment.configHome = detail::variable("XDG_CONFIG_HOME");
  environment.dataHome   = detail::variable("XDG_DATA_HOME");
  environment.cacheHome  = detail::variable("XDG_CACHE_HOME");
  environment.dataDirs   = detail::variable("XDG_DATA_DIRS");
  environment.configDirs = detail::variable("XDG_CONFIG_DIRS");
  if (auto const runtime = detail::variable("VIMRUNTIME")) {
    environment.vimRuntime = std::filesystem::path{*runtime};
  }
  if (auto const runtime = detail::variable("HELIX_RUNTIME")) {
    environment.helixRuntime = std::filesystem::path{*runtime};
  }
  return environment;
}


// Best-effort neovim plugin discovery helper.
[[nodiscard]] inline std::vector<std::filesystem::path>
nvimTreesitterUnder(LayoutEnvironment const& environment) {
  std::vector<std::filesystem::path> directories;

  if (auto const configHome =
        detail::baseDirectory(environment.configHome, environment.home, ".config")) {
    detail::appendIfAbsolute(directories, *configHome / "nvim" / "parser");
  }
  if (auto const dataHome =
        detail::baseDirectory(environment.dataHome, environment.home, ".local/share")) {
    detail::appendIfAbsolute(directories, *dataHome / "nvim" / "site" / "parser");
  }
  for (auto const& base :
       detail::splitList(environment.dataDirs, "/usr/local/share:/usr/share")) {
    detail::appendIfAbsolute(directories, base / "nvim" / "site" / "parser");
  }
  for (auto const& base : detail::splitList(environment.configDirs, "/etc/xdg")) {
    detail::appendIfAbsolute(directories, base / "nvim" / "parser");
  }

  // Not XDG. Distro packages install here, and the default source build
  // installs to the local prefix.
  directories.emplace_back("/usr/local/lib/nvim/parser");
  directories.emplace_back("/usr/lib/nvim/parser");

  // The parsers Neovim itself bundles, lowest priority.
  if (environment.vimRuntime) {
    detail::appendIfAbsolute(directories, *environment.vimRuntime / "parser");
  }

  detail::deduplicate(directories);
  return directories;
}


// Best-effort helix plugin discovery helper.
[[nodiscard]] inline std::vector<std::filesystem::path>
helixUnder(LayoutEnvironment const& environment) {
  std::vector<std::filesystem::path> directories;

  if (environment.helixRuntime) {
    detail::appendIfAbsolute(directories, *environment.helixRuntime / "grammars");
  }
  if (auto const configHome =
        detail::baseDirectory(environment.configHome, environment.home, ".config")) {
    detail::appendIfAbsolute(directories,
                             *configHome / "helix" / "runtime" / "grammars");
  }

  detail::deduplicate(directories);
  return directories;
}


// Best-effort tree-sitter-cli plugin discovery helper.
[[nodiscard]] inline std::vector<std::filesystem::path>
treeSitterCliUnder(LayoutEnvironment const& environment) {
  std::vector<std::filesystem::path> directories;

  if (auto const cacheHome =
        detail::baseDirectory(environment.cacheHome, environment.home, ".cache")) {
    detail::appendIfAbsolute(directories, *cacheHome / "tree-sitter" / "lib");
  }

  detail::deduplicate(directories);
  return directories;
}


[[nodiscard]] inline std::vector<std::filesystem::path>
nvimTreesitter() {
  return nvimTreesitterUnder(currentEnvironment());
}


[[nodiscard]] inline std::vector<std::filesystem::path>
helix() {
  return helixUnder(currentEnvironment());
}


[[nodiscard]] inline std::vector<std::filesystem::path>
treeSitterCli() {
  return treeSitterCliUnder(currentEnvironment());
}

}  // namespace layout

}  // namespace ts

#endif
