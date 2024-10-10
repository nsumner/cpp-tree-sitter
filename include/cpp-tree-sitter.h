#ifndef CPP_TREE_SITTER_H
#define CPP_TREE_SITTER_H

#include <iterator>
#include <memory>
#include <string_view>

#include <tree_sitter/api.h>

// Including the API directly already pollutes the namespace, but the
// functions are prefixed. Anything else that we include should be scoped
// within a namespace.

namespace ts {

/////////////////////////////////////////////////////////////////////////////
// Helper classes.
// These can be ignored while tring to understand the core APIs on demand.
/////////////////////////////////////////////////////////////////////////////


struct FreeHelper{
  template <typename T>
  void
  operator()(T* raw_pointer) const {
    std::free(raw_pointer);
  }
};


// An inclusive range representation
template<typename T>
struct Extent {
  T start;
  T end;
};


/////////////////////////////////////////////////////////////////////////////
// Aliases.
// Create slightly stricter aliases for some of the core tree-sitter types.
/////////////////////////////////////////////////////////////////////////////


// Direct alias of { row: uint32_t; column: uint32_t }
using Point = TSPoint;

using Symbol = uint16_t;

using Version = uint32_t;

using NodeID = uintptr_t;


// For types that manage resources, create custom wrappers that ensure
// clean-up. For types that can benefit from additional API discovery,
// wrappers with implicit conversion allow for automated method discovery.

struct Language {
  // NOTE: Allowing implicit conversions from TSLanguage to Language
  // improves the ergonomics a bit, as clients will still make use of the
  // custom language functions.

  /* implicit */ Language(TSLanguage const* language)
    : impl{language}
      { }

  [[nodiscard]] size_t
  getNumSymbols() const {
    return ts_language_symbol_count(impl);
  }

  [[nodiscard]] std::string_view
  getSymbolName(Symbol symbol) const {
    return ts_language_symbol_name(impl, symbol);
  }

  [[nodiscard]] Symbol
  getSymbolForName(std::string_view name, bool isNamed) const {
    return ts_language_symbol_for_name(impl,
                                       &name.front(),
                                       static_cast<uint32_t>(name.size()),
                                       isNamed);
  }

  [[nodiscard]] Version
  getVersion() const {
    return ts_language_version(impl);
  }

  TSLanguage const* impl;
};


class Cursor;

struct Node {
  explicit Node(TSNode node)
    : impl{node}
      { }

  ////////////////////////////////////////////////////////////////
  // Flag checks on nodes
  ////////////////////////////////////////////////////////////////
  [[nodiscard]] bool
  isNull() const {
    return ts_node_is_null(impl);
  }

  [[nodiscard]] bool
  isNamed() const {
    return ts_node_is_named(impl);
  }

  [[nodiscard]] bool
  isMissing() const {
    return ts_node_is_missing(impl);
  }

  [[nodiscard]] bool
  isExtra() const {
    return ts_node_is_extra(impl);
  }

  [[nodiscard]] bool
  hasError() const {
    return ts_node_has_error(impl);
  }

  [[nodiscard]] bool
  isError() const {
    return ts_node_is_error(impl);
  }

  ////////////////////////////////////////////////////////////////
  // Navigation
  ////////////////////////////////////////////////////////////////

  // Direct parent/sibling/child connections and cursors

  [[nodiscard]] Node
  getParent() const {
    return Node{ts_node_parent(impl)};
  }

  [[nodiscard]] Node
  getPreviousSibling() const {
    return Node{ts_node_prev_sibling(impl)};
  }

  [[nodiscard]] Node
  getNextSibling() const {
    return Node{ts_node_next_sibling(impl)};
  }

  [[nodiscard]] uint32_t
  getNumChildren() const {
    return ts_node_child_count(impl);
  }

  [[nodiscard]] Node
  getChild(uint32_t position) const {
    return Node{ts_node_child(impl, position)};
  }

  // Named children

  [[nodiscard]] uint32_t
  getNumNamedChildren() const {
    return ts_node_named_child_count(impl);
  }

  [[nodiscard]] Node
  getNamedChild(uint32_t position) const {
    return Node{ts_node_named_child(impl, position)};
  }

  // Named fields

  [[nodiscard]] std::string_view
  getFieldNameForChild(uint32_t child_position) const {
    return ts_node_field_name_for_child(impl, child_position);
  }

  [[nodiscard]] Node
  getChildByFieldName(std::string_view name) const {
    return Node{ts_node_child_by_field_name(impl,
                                            &name.front(),
                                            static_cast<uint32_t>(name.size()))};
  }

  // Definition deferred until after the definition of Cursor.
  [[nodiscard]] Cursor
  getCursor() const;

  ////////////////////////////////////////////////////////////////
  // Node attributes
  ////////////////////////////////////////////////////////////////

  // Returns a unique identifier for a node in a parse tree.
  // NodeIDs are numeric value types.
  [[nodiscard]] NodeID
  getID() const {
    return reinterpret_cast<NodeID>(impl.id);
  }

  // Returns an S-Expression representation of the subtree rooted at this node.
  [[nodiscard]] std::unique_ptr<char, FreeHelper>
  getSExpr() const {
    return std::unique_ptr<char,FreeHelper>{ts_node_string(impl)};
  }

  [[nodiscard]] Symbol
  getSymbol() const {
    return ts_node_symbol(impl);
  }

  [[nodiscard]] std::string_view
  getType() const {
    return ts_node_type(impl);
  }

  [[nodiscard]] Language
  getLanguage() const {
    return ts_node_language(impl);
  }

  [[nodiscard]] Extent<uint32_t>
  getByteRange() const {
    return {ts_node_start_byte(impl), ts_node_end_byte(impl)};
  }

  [[nodiscard]] Extent<Point>
  getPointRange() const {
    return {ts_node_start_point(impl), ts_node_end_point(impl)};
  }

  [[nodiscard]] std::string_view
  getSourceRange(std::string_view source) const {
    Extent<uint32_t> extents = this->getByteRange();
    return source.substr(extents.start, extents.end - extents.start);
  }

  TSNode impl;
};


class Tree {
public:
  Tree(TSTree* tree)
    : impl{tree, ts_tree_delete}
      { }

  [[nodiscard]] Node
  getRootNode() const {
    return Node{ts_tree_root_node(impl.get())};
  }

  [[nodiscard]] Language
  getLanguage() const {
    return Language{ts_tree_language(impl.get())};
  }

  [[nodiscard]] bool
  hasError() const {
    return getRootNode().hasError();
  }

private:
  std::unique_ptr<TSTree, decltype(&ts_tree_delete)> impl;
};


class Parser {
public:
  Parser(Language language)
    : impl{ts_parser_new(), ts_parser_delete} {
    ts_parser_set_language(impl.get(), language.impl);
  }

  [[nodiscard]] Tree
  parseString(std::string_view buffer) {
    return ts_parser_parse_string(
      impl.get(),
      nullptr,
      &buffer.front(),
      static_cast<uint32_t>(buffer.size())
    );
  }

private:
  std::unique_ptr<TSParser, decltype(&ts_parser_delete)> impl;
};


class Cursor {
public:
  Cursor(TSNode node)
    : impl{ts_tree_cursor_new(node)}
      { }

  Cursor(const TSTreeCursor& cursor)
    : impl{ts_tree_cursor_copy(&cursor)}
      { }

  // By default avoid copies until the ergonomics are clearer.
  Cursor(const Cursor& other) = delete;
  Cursor(Cursor&& other)
    : impl{} {
    std::swap(impl, other.impl);
  }
  Cursor& operator=(const Cursor& other)  = delete;
  Cursor& operator=(Cursor&& other) {
    std::swap(impl, other.impl);
    return *this;
  }

  ~Cursor() {
    ts_tree_cursor_delete(&impl);
  }

  void
  reset(Node node) {
    ts_tree_cursor_reset(&impl, node.impl);
  }

  void
  reset(Cursor& cursor) {
    ts_tree_cursor_reset_to(&impl, &cursor.impl);
  }

  [[nodiscard]] Cursor
  copy() const {
    return Cursor(impl);
  }

  [[nodiscard]] Node
  getCurrentNode() const {
    return Node{ts_tree_cursor_current_node(&impl)};
  }
  
  // Navigation

  [[nodiscard]] bool
  gotoParent() {
    return ts_tree_cursor_goto_parent(&impl);
  }

  [[nodiscard]] bool
  gotoNextSibling() {
    return ts_tree_cursor_goto_next_sibling(&impl);
  }

  [[nodiscard]] bool
  gotoPreviousSibling() {
    return ts_tree_cursor_goto_previous_sibling(&impl);
  }

  [[nodiscard]] bool
  gotoFirstChild() {
    return ts_tree_cursor_goto_first_child(&impl);
  }

  [[nodiscard]] bool
  gotoLastChild() {
    return ts_tree_cursor_goto_last_child(&impl);
  }

  [[nodiscard]] size_t
  getDepthFromOrigin() const {
    return ts_tree_cursor_current_depth(&impl);
  }

private:
  TSTreeCursor impl;
};

// To avoid cyclic dependencies and ODR violations, we define all methods 
// *using* Cursors inline after the definition of Cursor itself.
[[nodiscard]] Cursor
inline Node::getCursor() const {
  return Cursor{impl};
}


////////////////////////////////////////////////////////////////
// Child node iterators
////////////////////////////////////////////////////////////////

// These iterators make it possible to use C++ views on Nodes for
// easy processing.

class ChildIteratorSentinel { };

class ChildIterator {
public:
  using value_type = ts::Node;
  using difference_type = int;
  using iterator_category = std::input_iterator_tag;

  explicit ChildIterator(const ts::Node& node)
    : cursor{node.getCursor()},
      atEnd{!cursor.gotoFirstChild()}
      { }

  value_type
  operator*() const {
    return cursor.getCurrentNode();
  }

  ChildIterator&
  operator++() {
    atEnd = !cursor.gotoNextSibling();
    return *this;
  }

  ChildIterator&
  operator++(int) {
    atEnd = !cursor.gotoNextSibling();
    return *this;
  }

  friend bool operator== (const ChildIterator& a, const ChildIteratorSentinel&)   { return a.atEnd; }
  friend bool operator!= (const ChildIterator& a, const ChildIteratorSentinel& b) { return !(a == b); }
  friend bool operator== (const ChildIteratorSentinel& b, const ChildIterator& a) { return a == b; }
  friend bool operator!= (const ChildIteratorSentinel& b, const ChildIterator& a) { return a != b; }

private:
  ts::Cursor cursor;
  bool atEnd;
};


struct Children {
  using iterator = ChildIterator;
  using sentinel = ChildIteratorSentinel;

  auto begin() const -> iterator { return ChildIterator{node}; }
  auto end() const -> sentinel { return {}; }
  const ts::Node& node;
};

static_assert(std::input_iterator<ChildIterator>);
static_assert(std::sentinel_for<ChildIteratorSentinel, ChildIterator>);


}

#endif
