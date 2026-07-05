// The baseline visitor and folder treat all nodes uniformly and rely on
// the user to differentiate between different node types to perform
// different actions.
//
// To complement these, there are *typed* visiting and *typed* folding
// primitives that allow you to define the actions for visiting and folding
// based on the grammar type of the node. Dispatch to different types is
// automatic and supports the supertypes and hidden types within tree-sitter.
//
// ts::typed takes a set of per-type handlers and gives you back an ordinary
// TreeVisitor to use with ts::visit. Each construct in the grammar gets its
// own handler. ts::typedFold does the same for folds that produces a value.
// Dispatch costs a table lookup per node. The table is built once on
// visitor construction.
//
// The example below shows typed dispatch over a JSON tree. Writing one
// handler per node type with ts::typed, computing a value with
// ts::typedFold and letting a handler take over the walk.
//
//
// Building a visitor can fail
// ==========================================================================
// Both functions return std::expected. The type names in your handlers are
// checked against the language when the visitor is built, so a typo or a
// grammar change should be reported early in `error.name`.
//
// A typed visitor is tied to the language it was checked against. Using it
// on a tree from another language will dispatch to the wrong handlers.
//
//
// How the right handler is chosen
// ==========================================================================
// A handler is bound to a set of names and an event type (enter, leave),
// e.g. ts::on<"comment", "string">. During a visit or fold, a handler
// matches a node when one of its names is the node's type or when
// it names a supertype that covers the node's type. (Using supertypes
// requires a grammar built with language ABI 15 or newer.)
//
// If more than one handler could match, the most specific one wins. An exact
// type beats a supertype, a nearer supertype beats a distant one, either
// beats ts::otherwise, and ts::otherwise beats the default of carrying
// on with the walk. The order you list handlers in and the order of names
// within one handler does not change the outcome.
//
// A few smaller rules.
//
//   * One handler can name several types, as in ts::on<"comment", "string">.
//   * Naming the same type in two handlers is an error at construction. So
//     is a node type whose nearest supertype is claimed by two different
//     handlers, since there would be no way to choose between them.
//   * ERROR nodes match ts::on<"ERROR">, the same way the query language
//     writes (ERROR). With no such handler they go to ts::otherwise.
//   * ts::onLeave<...> handlers run on the way back out of a node at the
//     same point a plain visitor's onLeave would.
//
//
// Controlling the traversal yourself
// ==========================================================================
// The engine descends into a node's children for you. A handler that takes a
// ts::Walk& as a second argument takes back control over traversal, and the
// engine leaves its children alone. Call walk(child) to dispatch on a child
// whenever you want, in whatever order, and call walk.stop() to end the
// traversal early. Fold handlers take a ts::WalkFold<R>& instead, which
// gives you back the value folded from the child you pass it. There is an
// example of each below.
//
// NOTE: If a handler callable can be called both ways, e.g. a variadic
// generic lambda or an overloaded function object, the walk version wins and
// the engine will not descend for the nodes it matches. Take a plain
// (ts::Node) to be explicit on handlers that do not take control.
//
//
// Deep trees and the stack
// ==========================================================================
// ts::visit and ts::fold are iterative and keep their state on the heap, so
// depth costs you nothing. ts::typed and ts::typedFold are different,
// because ts::typedFold recurses once per node, and both recurse on custom
// handlers. Stack usage then follows the depth of the tree, and parse trees
// can get deep. Use ts::fold when the input is untrusted or machine
// generated, or check the depth yourself before descending.

#include <charconv>
#include <cstdlib>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <cpp-tree-sitter.h>
#include <cts/format.h>
#include <cts/typed.h>

extern "C" TSLanguage* tree_sitter_json();


int
main() {
  ts::Language const language{tree_sitter_json()};
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


  // We can define a visitor over different types of nodes using the classic
  // overload of lambdas idiom.
  int numbers = 0;
  int pairs = 0;
  auto counter = ts::typed(language,
    ts::on<"number">([&](ts::Node) { ++numbers; }),
    ts::on<"pair">([&](ts::Node) { ++pairs; }));

  // But it is possible for building this to fail.
  if (!counter) {
    std::println(stderr, "typed error: {}", counter.error());
    return EXIT_FAILURE;
  }

  // And then we can visit it as normal. The custom visitor handles dispatch.
  ts::visit(tree->getRootNode(), *counter);
  std::println("numbers={} pairs={}", numbers, pairs);


  // We can do the same kind of customization over typed folds instead.
  auto sum = ts::typedFold<int>(language,
    ts::on<"number">([&](ts::Node node) {
      // from_chars parses straight out of the source buffer and leaves
      // `value` untouched if the text does not scan as an int.
      std::string_view const text = node.getSourceRange(source);
      int value = 0;
      std::from_chars(text.data(), text.data() + text.size(), value);
      return value;
    }),
    ts::otherwise([](ts::Node node, ts::WalkFold<int>& walk) {
      int total = 0;
      for (ts::Node const child : node.getNamedChildren()) {
        total += walk(child);
      }
      return total;
    }));

  // Again, building them can fail.
  if (!sum) {
    std::println(stderr, "typedFold error: {}", sum.error());
    return EXIT_FAILURE;
  }

  // Running them is slightly more complicated than the base folds.
  std::println("sum={}", sum->run(tree->getRootNode()));


  // A handler taking a ts::Walk& owns descent into its own children. This one
  // visits an array's elements back to front. By default the engine would
  // have descended in order.
  std::vector<std::string> reversed;
  auto backwards = ts::typed(language,
    ts::on<"array">([](ts::Node node, ts::Walk& walk) {
      auto elements = node.getNamedChildren() | std::ranges::to<std::vector>();
      for (ts::Node const element : elements | std::views::reverse) {
        walk(element);
      }
    }),
    ts::on<"number">([&](ts::Node node) {
      reversed.emplace_back(node.getSourceRange(source));
    }),
    ts::otherwise([](ts::Node) { }));

  if (!backwards) {
    std::println(stderr, "typed error: {}", backwards.error());
    return EXIT_FAILURE;
  }

  // Numbers inside an array now arrive last-first. Numbers elsewhere are
  // untouched. Only the array handler changed how its own subtree is walked.
  ts::visit(tree->getRootNode(), *backwards);
  std::print("numbers in walk order:");
  for (const std::string& number : reversed) {
    std::print(" {}", number);
  }
  std::println("");
  return EXIT_SUCCESS;
}
