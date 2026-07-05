#ifndef CTS_NODE_H
#define CTS_NODE_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include <cts/common.h>
#include <cts/language.h>

namespace ts {

class Cursor;
class ChildrenView;


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

  // Direct parent/sibling/child connections and cursors.
  // These return std::nullopt on a miss (e.g. no parent, out-of-range
  // child). Definitions are deferred until after detail::asOptional.

  [[nodiscard]] std::optional<Node>
  getParent() const;

  [[nodiscard]] std::optional<Node>
  getPreviousSibling() const;

  [[nodiscard]] std::optional<Node>
  getNextSibling() const;

  [[nodiscard]] uint32_t
  getNumChildren() const {
    return ts_node_child_count(impl);
  }

  [[nodiscard]] std::optional<Node>
  getChild(uint32_t position) const;


  // Named children

  [[nodiscard]] uint32_t
  getNumNamedChildren() const {
    return ts_node_named_child_count(impl);
  }

  [[nodiscard]] std::optional<Node>
  getNamedChild(uint32_t position) const;


  // Named fields

  [[nodiscard]] std::optional<std::string_view>
  getFieldNameForChild(uint32_t position) const;

  [[nodiscard]] std::optional<std::string_view>
  getFieldNameForNamedChild(uint32_t position) const;

  [[nodiscard]] std::optional<Node>
  getChildByFieldName(std::string_view name) const;

  [[nodiscard]] std::optional<Node>
  getChildByFieldId(FieldId id) const;

  // Position based lookup

  [[nodiscard]] std::optional<Node>
  getDescendantForByteRange(Extent<uint32_t> range) const;

  [[nodiscard]] std::optional<Node>
  getNamedDescendantForByteRange(Extent<uint32_t> range) const;

  [[nodiscard]] std::optional<Node>
  getDescendantForPointRange(Extent<Point> range) const;

  [[nodiscard]] std::optional<Node>
  getNamedDescendantForPointRange(Extent<Point> range) const;


  // Adjust this node's cached coordinates after Tree::edit. Only needed for
  // Node values retrieved before the edit that must be used afterward.
  void
  edit(const InputEdit& inputEdit) {
    TSInputEdit const raw = inputEdit.toRaw();
    ts_node_edit(&impl, &raw);
  }

  // Child views over this node's (named) children.
  [[nodiscard]] ChildrenView getChildren() const;
  [[nodiscard]] ChildrenView getNamedChildren() const;

  // Definition deferred until after the definition of Cursor.
  [[nodiscard]] Cursor
  getCursor() const;


  ////////////////////////////////////////////////////////////////
  // Node attributes
  ////////////////////////////////////////////////////////////////

  // Returns a unique identifier for a node in a parse tree.
  // NodeIDs are opaque identity tokens: comparable and hashable, but not
  // arithmetic.
  [[nodiscard]] NodeID
  getID() const {
    return NodeID{reinterpret_cast<uintptr_t>(impl.id)};
  }

  // Nodes are fully equal when they are the same node of the same tree.
  friend bool
  operator==(Node lhs, Node rhs) noexcept {
    return ts_node_eq(lhs.impl, rhs.impl);
  }

  // Returns an S-Expression representation of the subtree rooted at this node.
  [[nodiscard]] std::string
  getSExpr() const {
    std::unique_ptr<char, FreeHelper> const raw{ts_node_string(impl)};
    return std::string{raw.get()};
  }

  [[nodiscard]] Symbol
  getSymbol() const {
    return Symbol{ts_node_symbol(impl)};
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
    return {.start=ts_node_start_byte(impl), .end=ts_node_end_byte(impl)};
  }

  [[nodiscard]] Extent<Point>
  getPointRange() const {
    return {.start=ts_node_start_point(impl), .end=ts_node_end_point(impl)};
  }

  [[nodiscard]] Range
  getRange() const {
    return Range{{.byte=ts_node_start_byte(impl),
                  .point=ts_node_start_point(impl)},
                 {.byte=ts_node_end_byte(impl),
                  .point=ts_node_end_point(impl)}};
  }

  [[nodiscard]] std::string_view
  getSourceRange(std::string_view source) const {
    Extent<uint32_t> const extents = this->getByteRange();
    return source.substr(extents.start, extents.end - extents.start);
  }

  TSNode impl;
};


// To avoid needing Node to be complete inside its own member declarations,
// detail::asOptional (and the navigation members that call it) are defined
// after the Node class.
namespace detail {
[[nodiscard]] inline std::optional<Node>
asOptional(TSNode node) {
  if (ts_node_is_null(node)) {
    return std::nullopt;
  }
  return Node{node};
}
}


inline std::optional<Node>
Node::getParent() const {
  return detail::asOptional(ts_node_parent(impl));
}

inline std::optional<Node>
Node::getPreviousSibling() const {
  return detail::asOptional(ts_node_prev_sibling(impl));
}

inline std::optional<Node>
Node::getNextSibling() const {
  return detail::asOptional(ts_node_next_sibling(impl));
}

inline std::optional<Node>
Node::getChild(uint32_t position) const {
  return detail::asOptional(ts_node_child(impl, position));
}

inline std::optional<Node>
Node::getNamedChild(uint32_t position) const {
  return detail::asOptional(ts_node_named_child(impl, position));
}


inline std::optional<std::string_view>
Node::getFieldNameForChild(uint32_t position) const {
  char const* name = ts_node_field_name_for_child(impl, position);
  if (name == nullptr) {
    return std::nullopt;
  }
  return std::string_view{name};
}


inline std::optional<std::string_view>
Node::getFieldNameForNamedChild(uint32_t position) const {
  char const* name = ts_node_field_name_for_named_child(impl, position);
  if (name == nullptr) {
    return std::nullopt;
  }
  return std::string_view{name};
}


inline std::optional<Node>
Node::getChildByFieldName(std::string_view name) const {
  // data() may be null, but the C API forbids this.
  char const* buffer = name.data() != nullptr ? name.data() : "";
  return detail::asOptional(ts_node_child_by_field_name(
    impl, buffer, static_cast<uint32_t>(name.size())));
}


inline std::optional<Node>
Node::getChildByFieldId(FieldId id) const {
  return detail::asOptional(
    ts_node_child_by_field_id(impl, std::to_underlying(id)));
}


inline std::optional<Node>
Node::getDescendantForByteRange(Extent<uint32_t> range) const {
  return detail::asOptional(
    ts_node_descendant_for_byte_range(impl, range.start, range.end));
}


inline std::optional<Node>
Node::getNamedDescendantForByteRange(Extent<uint32_t> range) const {
  return detail::asOptional(
    ts_node_named_descendant_for_byte_range(impl, range.start, range.end));
}


inline std::optional<Node>
Node::getDescendantForPointRange(Extent<Point> range) const {
  return detail::asOptional(
    ts_node_descendant_for_point_range(impl, range.start, range.end));
}


inline std::optional<Node>
Node::getNamedDescendantForPointRange(Extent<Point> range) const {
  return detail::asOptional(
    ts_node_named_descendant_for_point_range(impl, range.start, range.end));
}

}

#endif
