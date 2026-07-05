#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <doctest/doctest.h>
#include <cpp-tree-sitter.h>


namespace {

struct Doubled {
  int value;
  friend bool operator==(Doubled, Doubled) = default;
};


struct ToDoubled {
  constexpr Doubled
  operator()(int raw) const noexcept {
    return Doubled{raw * 2};
  }
};


using DoubledRange = ts::detail::MappedRange<int, ToDoubled>;

constexpr int table[4]{1, 2, 3, 4};

}  // namespace


static_assert(std::random_access_iterator<std::ranges::iterator_t<DoubledRange>>);
static_assert(std::ranges::random_access_range<DoubledRange>);
static_assert(std::ranges::sized_range<DoubledRange>);
static_assert(!std::ranges::contiguous_range<DoubledRange>);

static_assert(std::ranges::borrowed_range<DoubledRange>);

constexpr DoubledRange compileTime =
  ts::detail::makeRange<DoubledRange>(table, 4);
static_assert(compileTime.size() == 4);
static_assert(compileTime[2] == Doubled{6});
static_assert(*compileTime.begin() == Doubled{2});

static_assert(noexcept(*std::declval<std::ranges::iterator_t<DoubledRange> const&>()));


TEST_CASE("MappedRange converts each element on demand") {
  auto range = ts::detail::makeRange<DoubledRange>(table, 4);
  REQUIRE(range.size() == 4);
  CHECK(range.front().value == 2);
  CHECK(range.back().value == 8);
  CHECK(range[3].value == 8);

  std::vector<int> seen;
  for (Doubled const element : range) {
    seen.push_back(element.value);
  }
  CHECK(seen == std::vector<int>{2, 4, 6, 8});
}


TEST_CASE("MappedRange is empty for a null, zero-length array") {
  auto range = ts::detail::makeRange<DoubledRange>(nullptr, 0);
  CHECK(range.empty());
  CHECK(std::ranges::begin(range) == std::ranges::end(range));
}
