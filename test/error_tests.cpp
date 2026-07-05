#include <string_view>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>


TEST_CASE("Error carries a kind, an offset, and a human-readable message") {
  ts::Error const error{.kind=ts::ErrorKind::QuerySyntax, .offset=17};
  CHECK(error.kind == ts::ErrorKind::QuerySyntax);
  CHECK(error.offset == 17);
  CHECK(!error.message().empty());

  ts::Error const parseError{.kind=ts::ErrorKind::ParseFailed};
  CHECK(parseError.offset == 0);
  CHECK(parseError.message() != error.message());
}


static_assert(std::is_trivially_copyable_v<ts::Error>);


TEST_CASE("Error::hasOffset marks exactly the kinds that carry an offset") {
  CHECK(ts::Error{ts::ErrorKind::QuerySyntax, 12}.hasOffset());
  CHECK(ts::Error{ts::ErrorKind::QueryNodeType}.hasOffset());
  CHECK(ts::Error{ts::ErrorKind::QueryField}.hasOffset());
  CHECK(ts::Error{ts::ErrorKind::QueryCapture}.hasOffset());
  CHECK(ts::Error{ts::ErrorKind::QueryStructure}.hasOffset());
  CHECK(ts::Error{ts::ErrorKind::QueryLanguage}.hasOffset());

  CHECK(!ts::Error{ts::ErrorKind::LanguageIncompatible}.hasOffset());
  CHECK(!ts::Error{ts::ErrorKind::ParseFailed}.hasOffset());
  CHECK(!ts::Error{ts::ErrorKind::QueryInvalidRange}.hasOffset());
  CHECK(!ts::Error{ts::ErrorKind::QueryNodeLanguage}.hasOffset());
  CHECK(!ts::Error{ts::ErrorKind::VisitorNodeType}.hasOffset());
  CHECK(!ts::Error{ts::ErrorKind::VisitorSupertypeAmbiguity}.hasOffset());
  CHECK(!ts::Error{ts::ErrorKind::VisitorDuplicate}.hasOffset());
  CHECK(!ts::Error{ts::ErrorKind::QueryPredicatesNeedSource}.hasOffset());
  CHECK(!ts::Error{ts::ErrorKind::QueryPredicatesNeedRegex}.hasOffset());

  CHECK(ts::Error{ts::ErrorKind::QueryPredicate, 4}.hasOffset());
  CHECK(ts::Error{ts::ErrorKind::QueryPredicateRegex, 4}.hasOffset());
}


TEST_CASE("Error::hasName marks exactly the kinds that carry a name") {
  CHECK(ts::Error{ts::ErrorKind::VisitorNodeType}.hasName());
  CHECK(ts::Error{ts::ErrorKind::VisitorSupertypeAmbiguity}.hasName());
  CHECK(ts::Error{ts::ErrorKind::VisitorDuplicate}.hasName());

  CHECK(!ts::Error{ts::ErrorKind::LanguageIncompatible}.hasName());
  CHECK(!ts::Error{ts::ErrorKind::ParseFailed}.hasName());
  CHECK(!ts::Error{ts::ErrorKind::QuerySyntax, 12}.hasName());
  CHECK(!ts::Error{ts::ErrorKind::QueryNodeType}.hasName());
  CHECK(!ts::Error{ts::ErrorKind::QueryField}.hasName());
  CHECK(!ts::Error{ts::ErrorKind::QueryCapture}.hasName());
  CHECK(!ts::Error{ts::ErrorKind::QueryStructure}.hasName());
  CHECK(!ts::Error{ts::ErrorKind::QueryLanguage}.hasName());
  CHECK(!ts::Error{ts::ErrorKind::QueryInvalidRange}.hasName());
  CHECK(!ts::Error{ts::ErrorKind::QueryNodeLanguage}.hasName());
  CHECK(!ts::Error{ts::ErrorKind::QueryPredicate, 4}.hasName());
  CHECK(!ts::Error{ts::ErrorKind::QueryPredicateRegex, 4}.hasName());
  CHECK(!ts::Error{ts::ErrorKind::QueryPredicatesNeedSource}.hasName());
  CHECK(!ts::Error{ts::ErrorKind::QueryPredicatesNeedRegex}.hasName());
}
