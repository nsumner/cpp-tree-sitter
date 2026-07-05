#ifndef CTS_CURSOR_H
#define CTS_CURSOR_H

#include <concepts>
#include <cstddef>
#include <utility>

#include <tree_sitter/api.h>

#include <cts/node.h>

namespace ts {

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
  Cursor& operator=(const Cursor& other)  = delete;

  // After a move, `other` is left in a valid but unspecified state.
  Cursor(Cursor&& other) noexcept
    : impl{other.impl} {
    other.impl = TSTreeCursor{};
  }

  Cursor& operator=(Cursor&& other) noexcept {
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
  reset(const Cursor& cursor) {
    ts_tree_cursor_reset_to(&impl, &cursor.impl);
  }


  [[nodiscard]] Cursor
  copy() const {
    return { impl };
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


static_assert(!std::copyable<Cursor>);
static_assert(std::movable<Cursor>);


// To avoid cyclic dependencies and ODR violations, we define all methods
// *using* Cursors inline after the definition of Cursor itself.
[[nodiscard]] Cursor
inline Node::getCursor() const {
  return Cursor{impl};
}

}

#endif
