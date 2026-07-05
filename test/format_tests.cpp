#include <array>
#include <cstdlib>
#include <format>
#include <new>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>
#include <cts/format.h>
#include <cts/query/format.h>

#include <stringify.h>


extern "C" TSLanguage* tree_sitter_json();
extern "C" TSLanguage* tree_sitter_ctstest();  // the ABI-15 grammar


namespace {
template <typename T>
std::string
rendered(T const& value) {
  return std::string{doctest::toString(value).c_str()};
}
}  // namespace


TEST_CASE("doctest renders cts values in failure messages") {
  CHECK(rendered(ts::Point{3, 7}) == "3:7");
  CHECK(rendered(ts::Extent<uint32_t>{0, 9}) == "[0, 9)");
  CHECK(rendered(ts::Location{9, ts::Point{0, 9}}) == "9 (0:9)");
  CHECK(rendered(ts::Range{ts::Location{0, ts::Point{0, 0}},
                           ts::Location{9, ts::Point{0, 9}}})
        == "[0 (0:0), 9 (0:9))");
  CHECK(rendered(ts::Error{ts::ErrorKind::QuerySyntax, 12})
        == "query syntax error at offset 12");

  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  std::string_view const source = "[1, null]";
  const auto tree = parser->parse(source);
  REQUIRE(tree.has_value());
  CHECK(rendered(tree->getRootNode()) == "document [0, 9)");
}


TEST_CASE("doctest renders identifiers without help from this header") {
  ts::Language const language = tree_sitter_json();
  auto arraySymbol = language.getSymbolForName("array", true);
  REQUIRE(arraySymbol.has_value());
  CHECK(rendered(*arraySymbol)
        == std::format("{}", std::to_underlying(*arraySymbol)));
  CHECK(rendered(ts::FieldId::None) == "0");
}


TEST_CASE("Point formats as row:column") {
  CHECK(std::format("{}", ts::Point{3, 7}) == "3:7");
  CHECK(std::format("{}", ts::Point{}) == "0:0");
}


TEST_CASE("Extent formats as a half-open range") {
  CHECK(std::format("{}", ts::Extent<uint32_t>{0, 42}) == "[0, 42)");
  CHECK(std::format("{}", ts::Extent<ts::Point>{ts::Point{1, 2},
                                                ts::Point{3, 4}})
        == "[1:2, 3:4)");
}


TEST_CASE("Location formats as byte and point together") {
  CHECK(std::format("{}", ts::Location{42, ts::Point{3, 7}})
        == "42 (3:7)");

  ts::Range range{ts::Location{.byte=0, .point=ts::Point{0, 0}},
                  ts::Location{.byte=9, .point=ts::Point{0, 9}}};
  CHECK(std::format("{}", range) == "[0 (0:0), 9 (0:9))");
}


TEST_CASE("Identifiers format as their underlying integer") {
  ts::Language const language = tree_sitter_json();
  auto arraySymbol = language.getSymbolForName("array", true);
  REQUIRE(arraySymbol.has_value());

  CHECK(std::format("{}", *arraySymbol)
        == std::format("{}", std::to_underlying(*arraySymbol)));

  CHECK(std::format("{:x}", ts::Symbol{255}) == "ff");
  CHECK(std::format("{:>5}", ts::FieldId{7}) == "    7");
  CHECK(std::format("{}", ts::NodeID{0}) == "0");
}


#if defined(__cpp_lib_format_ranges)

TEST_CASE("A SymbolRange formats as a range of integers") {
  ts::Language const language = tree_sitter_ctstest();
  REQUIRE(language.getAbiVersion() >= 15);
  auto literal = language.getSymbolForName("_literal", true);
  REQUIRE(literal.has_value());

  std::string const rendered = std::format("{}", language.getSubtypes(*literal));
  CHECK(rendered.starts_with("["));
  CHECK(rendered.ends_with("]"));
  CHECK(rendered.find('"') == std::string::npos);  // integers but unquoted
}

#endif  // __cpp_lib_format_ranges


TEST_CASE("Composite formatters accept the standard string grammar") {
  CHECK(std::format("{:>8}", ts::Point{3, 7}) == "     3:7");
  CHECK(std::format("{:<8}", ts::Point{3, 7}) == "3:7     ");
  CHECK(std::format("{:^9}", ts::Point{3, 7}) == "   3:7   ");
  CHECK(std::format("{:*>8}", ts::Point{3, 7}) == "*****3:7");
  // A width smaller than the rendering does not truncate.
  CHECK(std::format("{:2}", ts::Point{3, 7}) == "3:7");
  // Precision does truncate, exactly as on a string_view.
  CHECK(std::format("{:.2}", ts::Point{3, 7}) == "3:");
  // `s` is the string presentation type, which is the default rendering.
  CHECK(std::format("{:s}", ts::Point{3, 7}) == "3:7");
  // Dynamic width resolves through the same format context.
  CHECK(std::format("{:>{}}", ts::Point{3, 7}, 8) == "     3:7");
}


TEST_CASE("Error prints its offset only for the kinds that carry one") {
  CHECK(std::format("{}", ts::Error{ts::ErrorKind::QuerySyntax, 12})
        == "query syntax error at offset 12");
  CHECK(std::format("{}", ts::Error{ts::ErrorKind::QueryCapture, 0})
        == "query references an undefined capture name at offset 0");
  CHECK(std::format("{}", ts::Error{ts::ErrorKind::ParseFailed})
        == "parsing produced no tree");
  CHECK(std::format("{}", ts::Error{ts::ErrorKind::VisitorDuplicate})
        == "typed visitor has duplicate handlers for the same node type");
}


TEST_CASE("Errors that name a type format with it appended") {
  CHECK(std::format("{}", ts::Error{.kind = ts::ErrorKind::VisitorNodeType,
                                    .name = "nubmer"})
        == "typed visitor references an unknown node type: nubmer");
  CHECK(std::format("{}", ts::Error{.kind = ts::ErrorKind::VisitorDuplicate,
                                    .name = "number"})
        == "typed visitor has duplicate handlers for the same node type: "
           "number");
  // A name-carrying kind with nothing to report stays clean, and kinds that
  // never carry one are unaffected.
  CHECK(std::format("{}", ts::Error{ts::ErrorKind::VisitorNodeType})
        == "typed visitor references an unknown node type");
  CHECK(std::format("{}", ts::Error{ts::ErrorKind::QuerySyntax, 12})
        == "query syntax error at offset 12");
}


TEST_CASE("Enumerations format as their enumerator name") {
  CHECK(std::format("{}", ts::SymbolType::Regular) == "Regular");
  CHECK(std::format("{}", ts::SymbolType::Anonymous) == "Anonymous");
  CHECK(std::format("{}", ts::SymbolType::Supertype) == "Supertype");
  CHECK(std::format("{}", ts::SymbolType::Auxiliary) == "Auxiliary");

  CHECK(std::format("{}", ts::ErrorKind::QuerySyntax) == "QuerySyntax");
  CHECK(std::format("{}", ts::ErrorKind::LanguageIncompatible)
        == "LanguageIncompatible");

  CHECK(std::format("{}", ts::VisitAction::Continue) == "Continue");
  CHECK(std::format("{}", ts::VisitAction::SkipChildren) == "SkipChildren");
  CHECK(std::format("{}", ts::VisitAction::Stop) == "Stop");

  CHECK(std::format("{}", ts::ChildScope::All) == "All");
  CHECK(std::format("{}", ts::ChildScope::NamedOnly) == "NamedOnly");

  CHECK(std::format("{:>10}", ts::VisitAction::Stop) == "      Stop");
}


TEST_CASE("Node formats as its type and byte range") {
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  std::string_view const source = "[1, null]";
  const auto tree = parser->parse(source);
  REQUIRE(tree.has_value());

  ts::Node root = tree->getRootNode();
  CHECK(std::format("{}", root) == "document [0, 9)");

  ts::Node array = root.getNamedChild(0).value();
  CHECK(std::format("{}", array) == "array [0, 9)");
  CHECK(std::format("{:>20}", array) == "        array [0, 9)");
}


#if defined(__cpp_lib_format_ranges)

TEST_CASE("Ranges of nodes format element-wise") {
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  std::string_view const source = "[1, null]";
  const auto tree = parser->parse(source);
  REQUIRE(tree.has_value());
  ts::Node const array = tree->getRootNode().getNamedChild(0).value();

  std::vector<ts::Node> children;
  for (ts::Node const child : array.getNamedChildren()) {
    children.push_back(child);
  }
  CHECK(std::format("{}", children) == "[number [1, 2), null [4, 8)]");

  CHECK(std::format("{}", array.getNamedChildren())
        == "[number [1, 2), null [4, 8)]");

  CHECK(std::format("{}", children).find('"') == std::string::npos);

  CHECK(std::format("{}", children
                          | std::views::transform(&ts::Node::getSExpr))
        == "[\"(number)\", \"(null)\"]");
}

#endif  // __cpp_lib_format_ranges


TEST_CASE("query ids format as their underlying numbers") {
  CHECK(std::format("{}", ts::CaptureId{3}) == "3");
  CHECK(std::format("{}", ts::PredicateTokenId{1}) == "1");
  CHECK(std::format("{}", ts::PatternIndex{0}) == "0");
  CHECK(std::format("{:03}", ts::CaptureId{7}) == "007");
}


TEST_CASE("Quantifier formats as its name") {
  CHECK(std::format("{}", ts::Quantifier::Zero) == "Zero");
  CHECK(std::format("{}", ts::Quantifier::ZeroOrOne) == "ZeroOrOne");
  CHECK(std::format("{}", ts::Quantifier::ZeroOrMore) == "ZeroOrMore");
  CHECK(std::format("{}", ts::Quantifier::One) == "One");
  CHECK(std::format("{}", ts::Quantifier::OneOrMore) == "OneOrMore");
  CHECK(std::format("{:>6}", ts::Quantifier::One) == "   One");
}


TEST_CASE("ProgressAction formats as its name") {
  CHECK(std::format("{}", ts::ProgressAction::Continue) == "Continue");
  CHECK(std::format("{}", ts::ProgressAction::Cancel) == "Cancel");
  CHECK(std::format("{:>8}", ts::ProgressAction::Cancel) == "  Cancel");
}


TEST_CASE("QueryCapture and QueryMatch format as what they carry") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  std::string const source = R"({"a": 1})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(
    tree_sitter_json(), "(pair key: (string) @key value: (_) @value)").value();

  ts::QueryCursor cursor;
  size_t matches = 0;
  for (const ts::QueryMatch& match :
       cursor.getMatches(query, tree.getRootNode()).value()) {
    CHECK(std::format("{}", match.getCaptures()[0]) == "@0 string [1, 4)");
    CHECK(std::format("{}", match) == "pattern 0, 2 captures");
    ++matches;
  }
  CHECK(matches == 1);
}


TEST_CASE("CaptureResult formats as its match and position") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  std::string const source = R"({"a": 1})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(
    tree_sitter_json(), "(pair key: (string) @key value: (_) @value)").value();

  ts::QueryCursor cursor;
  size_t results = 0;
  for (const ts::CaptureResult& result :
       cursor.getCaptureStream(query, tree.getRootNode()).value()) {
    CHECK(std::format("{}", result)
          == std::format("{}, capture {}", result.match, result.position));
    ++results;
  }
  CHECK(results == 2);
}


TEST_CASE("A predicate argument formats as a capture or a token") {
  CHECK(std::format("{}", ts::PredicateArg{ts::CaptureId{3}}) == "@3");
  CHECK(std::format("{}", ts::PredicateArg{ts::PredicateTokenId{7}})
        == "token 7");
}


TEST_CASE("A predicate formats as its name and arity") {
  auto query = ts::Query::create(tree_sitter_json(),
    R"(((pair key: (string) @k) (#eq? @k "\"a\"")))");
  REQUIRE(query.has_value());
  auto predicates = query->getPredicates(ts::PatternIndex{0});
  REQUIRE(predicates.has_value());
  auto first = *std::ranges::begin(*predicates);
  CHECK(std::format("{}", first) == "#eq? with 2 arguments");
}


#if defined(__SANITIZE_ADDRESS__)
#  define CTS_TEST_SKIP_ALLOCATION_CHECK 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define CTS_TEST_SKIP_ALLOCATION_CHECK 1
#  endif
#endif

#ifndef CTS_TEST_SKIP_ALLOCATION_CHECK
namespace {
size_t allocationCount = 0;
bool countingAllocations = false;
}

void*
operator new(size_t size) {
  if (countingAllocations) {
    ++allocationCount;
  }
  void* const memory = std::malloc(size);
  if (memory == nullptr) {
    throw std::bad_alloc{};
  }
  return memory;
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }


TEST_CASE("Formatting a node with an empty spec allocates nothing") {
  auto parser = ts::Parser::create(tree_sitter_json());
  REQUIRE(parser.has_value());
  std::string_view const source = "[1, null]";
  const auto tree = parser->parse(source);
  REQUIRE(tree.has_value());
  ts::Node root = tree->getRootNode();

  std::array<char, 128> buffer{};

  allocationCount = 0;
  countingAllocations = true;
  std::format_to(buffer.data(), "{}", 42);
  countingAllocations = false;
  size_t const baseline = allocationCount;
  REQUIRE(baseline == 0);

  allocationCount = 0;
  countingAllocations = true;
  auto const end = std::format_to(buffer.data(), "{}", root);
  countingAllocations = false;

  CHECK(std::string_view(buffer.data(), end) == "document [0, 9)");
  CHECK(allocationCount == 0);
}


TEST_CASE("Subtype queries stay allocation-free") {
  ts::Language const language = tree_sitter_ctstest();
  REQUIRE(language.getAbiVersion() >= 15);
  auto literal = language.getSymbolForName("_literal", true);
  REQUIRE(literal.has_value());

  size_t count = 0;
  allocationCount = 0;
  countingAllocations = true;
  for (ts::Symbol const sub : language.getSubtypes(*literal)) {
    count += std::to_underlying(sub) != 0 ? 1u : 0u;
  }
  countingAllocations = false;

  CHECK(count > 0);
  CHECK(allocationCount == 0);
}


TEST_CASE("Query captures stay allocation-free") {
  auto parser = ts::Parser::create(tree_sitter_json()).value();
  std::string const source = R"({"a": 1, "b": 2})";
  const auto tree = parser.parse(source).value();
  auto query = ts::Query::create(
    tree_sitter_json(), "(pair key: (string) @key)").value();
  ts::QueryCursor cursor;

  size_t count = 0;
  allocationCount = 0;
  countingAllocations = true;
  for (const ts::QueryMatch& match : cursor.getMatches(query, tree.getRootNode()).value()) {
    for (const ts::QueryCapture& capture : match.getCaptures()) {
      count += capture.id == ts::CaptureId{0} ? 1u : 0u;
    }
  }
  countingAllocations = false;

  CHECK(count == 2);
  CHECK(allocationCount == 0);
}
#endif
