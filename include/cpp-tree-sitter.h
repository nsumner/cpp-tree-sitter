#ifndef CPP_TREE_SITTER
#define CPP_TREE_SITTER

#include <memory>
#include <string_view>

#include <tree_sitter/api.h>
#include <tree_sitter/parser.h>

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
  const T start;
  const T end;
};


/////////////////////////////////////////////////////////////////////////////
// Aliases.
// Create slightly stricter aliases for some of the core tree-sitter types.
/////////////////////////////////////////////////////////////////////////////

// Add const enforcement on the underlying language. Note that TSLanguages
// are static and never deallocated, so there are no resources to manage.
using Language = TSLanguage const *;

// Direct alias of { row: uint32_t; column: uint32_t }
using Point = TSPoint;

using Symbol = uint16_t;

using NodeID = uintptr_t;


// For types that manage resources, create custom wrappers that ensure
// clean-up. For types that can benefit from additional API discovery,
// wrappers with implicit conversion allow for automated method discovery.


class Cursor;

struct Node {
  Node(TSNode node)
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

  // TODO: Not yet available in last release
  // [[nodiscard]] bool
  // isError() const {
  //   return ts_node_is_error(impl);
  // }

  ////////////////////////////////////////////////////////////////
  // Navigation
  ////////////////////////////////////////////////////////////////

  // Direct parent/sibling/child connections and cursors

  [[nodiscard]] Node
  getParent() const {
    return ts_node_parent(impl);
  }

  [[nodiscard]] Node
  getPreviousSibling() const {
    return ts_node_prev_sibling(impl);
  }

  [[nodiscard]] Node
  getNextSibling() const {
    return ts_node_next_sibling(impl);
  }

  [[nodiscard]] uint32_t
  getNumChildren() const {
    return ts_node_child_count(impl);
  }

  [[nodiscard]] Node
  getChild(uint32_t position) const {
    return ts_node_child(impl, position);
  }

  // Named children

  [[nodiscard]] uint32_t
  getNumNamedChildren() const {
    return ts_node_named_child_count(impl);
  }

  [[nodiscard]] Node
  getNamedChild(uint32_t position) const {
    return ts_node_named_child(impl, position);
  }

  // Named fields

  [[nodiscard]] std::string_view
  getFieldNameForChild(uint32_t child_position) const {
    return ts_node_field_name_for_child(impl, child_position);
  }

  [[nodiscard]] Node
  getChildByFieldName(std::string_view name) const {
    return ts_node_child_by_field_name(impl,
                                       &name.front(),
                                       static_cast<uint32_t>(name.size()));
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

  // TODO: Not yet available in last release
  // Language
  // getLanguage() const {
  //   return ts_node_language(impl);
  // }

  [[nodiscard]] Extent<uint32_t>
  getByteRange() const {
    return {ts_node_start_byte(impl), ts_node_end_byte(impl)};
  }

  [[nodiscard]] Extent<Point>
  getPointRange() const {
    return {ts_node_start_point(impl), ts_node_end_point(impl)};
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
    return ts_tree_root_node(impl.get());
  }

  [[nodiscard]] Language
  getLanguage() const {
    return ts_tree_language(impl.get());
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
    ts_parser_set_language(impl.get(), language);
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

  // By default avoid copies and moves until the ergonomics are clearer.
  Cursor(const Cursor&) = delete;
  Cursor(Cursor&&) = delete;
  Cursor& operator=(const Cursor& other) = delete;
  Cursor& operator=(Cursor&& other) = delete;

  ~Cursor() {
    ts_tree_cursor_delete(&impl);
  }

  void
  reset(Node node) {
    ts_tree_cursor_reset(&impl, node.impl);
  }

  // TODO: Not yet available in last release
  // void
  // reset(Cursor& cursor) {
  //   ts_tree_cursor_reset_to(&impl, &cursor.impl);
  // }

  Cursor
  copy() const {
    return Cursor(impl);
  }

  Node
  getCurrentNode() const {
    return Node{ts_tree_cursor_current_node(&impl)};
  }
  
  // Navigation

  bool
  gotoParent() {
    return ts_tree_cursor_goto_parent(&impl);
  }

  bool
  gotoNextSibling() {
    return ts_tree_cursor_goto_next_sibling(&impl);
  }

  // TODO: Not yet available in last release
  // bool
  // gotoPreviousSibling() {
  //   return ts_tree_cursor_goto_previous_sibling(&impl);
  // }

  bool
  gotoFirstChild() {
    return ts_tree_cursor_goto_first_child(&impl);
  }

  // TODO: Not yet available in last release
  // bool
  // gotoLastChild() {
  //   return ts_tree_cursor_goto_last_child(&impl);
  // }

  // TODO: Not yet available in last release
  // size_t
  // getDepthFromOrigin() const {
  //   return ts_tree_cursor_current_depth(&impl);
  // }

private:
  TSTreeCursor impl;
};

// To avoid cyclic dependencies and ODR violations, we define all methods 
// *using* Cursors inline after the definition of Cursor itself.
[[nodiscard]] Cursor
inline Node::getCursor() const {
  return Cursor{impl};
}

}

#endif
