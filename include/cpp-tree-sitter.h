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
using Language = TSLanguage const * const;

// Direct alias of { row: uint32_t; column: uint32_t }
using Point = TSPoint;

using Symbol = uint16_t;


// For types that manage resources, create custom wrappers that ensure
// clean-up. For types that can benefit from additional API discovery,
// wrappers with implicit conversion allow for automated method discovery.

struct Node {
  Node(TSNode node)
    : impl{node}
      { }

  ////////////////////////////////////////////////////////////////
  // Flag checks on nodes
  ////////////////////////////////////////////////////////////////
  bool
  isNull() const {
    return ts_node_is_null(impl);
  }

  bool
  isNamed() const {
    return ts_node_is_named(impl);
  }

  bool
  isMissing() const {
    return ts_node_is_missing(impl);
  }

  bool
  isExtra() const {
    return ts_node_is_extra(impl);
  }

  bool
  hasError() const {
    return ts_node_has_error(impl);
  }

  // TODO: Not yet availale in last release
  // bool
  // isError() const {
  //   return ts_node_is_error(impl);
  // }

  ////////////////////////////////////////////////////////////////
  // Navigation
  ////////////////////////////////////////////////////////////////

  // Direct parent/child connections

  Node
  getParent() const {
    return ts_node_parent(impl);
  }

  uint32_t
  getNumChildren() const {
    return ts_node_child_count(impl);
  }

  Node
  getChild(uint32_t position) const {
    return ts_node_child(impl, position);
  }

  // Named children

  uint32_t
  getNumNamedChildren() const {
    return ts_node_named_child_count(impl);
  }

  Node
  getNamedChild(uint32_t position) const {
    return ts_node_named_child(impl, position);
  }

  // Named fields

  std::string_view
  getFieldNameForChild(uint32_t child_position) const {
    return ts_node_field_name_for_child(impl, child_position);
  }

  Node
  getChildByFieldName(std::string_view name) const {
    return ts_node_child_by_field_name(impl,
                                       &name.front(),
                                       static_cast<uint32_t>(name.size()));
  }

  ////////////////////////////////////////////////////////////////
  //
  ////////////////////////////////////////////////////////////////

  std::unique_ptr<char, FreeHelper>
  getString() const {
    return std::unique_ptr<char,FreeHelper>{ts_node_string(impl)};
  }

  Symbol
  getSymbol() const {
    return ts_node_symbol(impl);
  }

  std::string_view
  getType() const {
    return ts_node_type(impl);
  }

  // TODO: Not yet available in last release
  // Language
  // getLanguage() const {
  //   return ts_node_language(impl);
  // }

  Extent<uint32_t>
  getByteRange() const {
    return {ts_node_start_byte(impl), ts_node_end_byte(impl)};
  }

  Extent<Point>
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

  Node
  getRootNode() const {
    return ts_tree_root_node(impl.get());
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

  Tree
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

}

#endif
