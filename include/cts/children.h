#ifndef CTS_CHILDREN_H
#define CTS_CHILDREN_H

#include <iterator>
#include <ranges>

#include <cts/cursor.h>
#include <cts/node.h>

namespace ts {

enum class ChildScope : bool { All, NamedOnly };


// A lazy, cursor-driven, single-pass iterator over a node's children.
// Move-only because it owns its Cursor. For random access or multi-pass
// iteration, materialize: children | std::ranges::to<std::vector>().
class ChildIterator {
public:
  using value_type = Node;
  using difference_type = std::ptrdiff_t;
  using iterator_concept = std::input_iterator_tag;

  ChildIterator(const Node& node, ChildScope scope)
    : cursor{node.getCursor()},
      scope{scope},
      atEnd{!cursor.gotoFirstChild()} {
    skipFiltered();
  }


  value_type
  operator*() const {
    return cursor.getCurrentNode();
  }


  ChildIterator&
  operator++() {
    advance();
    return *this;
  }

  void
  operator++(int) {
    advance();
  }


  friend bool
  operator==(const ChildIterator& it, std::default_sentinel_t) {
    return it.atEnd;
  }

private:
  void
  advance() {
    atEnd = !cursor.gotoNextSibling();
    skipFiltered();
  }


  void
  skipFiltered() {
    if (scope == ChildScope::NamedOnly) {
      while (!atEnd && !cursor.getCurrentNode().isNamed()) {
        atEnd = !cursor.gotoNextSibling();
      }
    }
  }

  Cursor cursor;
  ChildScope scope;
  bool atEnd;
};


// Holds the parent by value so views over temporaries don't dangle.
class ChildrenView : public std::ranges::view_interface<ChildrenView> {
public:
  explicit ChildrenView(Node node, ChildScope scope = ChildScope::All)
    : node{node}, scope{scope}
      { }

  [[nodiscard]] ChildIterator begin() const { return ChildIterator{node, scope}; }
  [[nodiscard]] std::default_sentinel_t end() const { return {}; }

private:
  Node node;
  ChildScope scope;
};


static_assert(std::input_iterator<ChildIterator>);
static_assert(std::sentinel_for<std::default_sentinel_t, ChildIterator>);
static_assert(std::ranges::input_range<ChildrenView>);
static_assert(std::ranges::view<ChildrenView>);
static_assert(std::copyable<ChildrenView>);


[[nodiscard]] inline ChildrenView
Node::getChildren() const {
  return ChildrenView{*this, ChildScope::All};
}


[[nodiscard]] inline ChildrenView
Node::getNamedChildren() const {
  return ChildrenView{*this, ChildScope::NamedOnly};
}


// A lazy, cursor-driven, single-pass, pre-order walk over root and its
// descendants. Move-only because it owns its Cursor. For random access or
// multi-pass iteration, materialize it with:
//   preorder(root) | std::ranges::to<std::vector>().
class PreorderIterator {
public:
  using value_type = Node;
  using difference_type = std::ptrdiff_t;
  using iterator_concept = std::input_iterator_tag;

  explicit PreorderIterator(const Node& root)
    : cursor{root.getCursor()}
      { }


  value_type
  operator*() const {
    return cursor.getCurrentNode();
  }


  PreorderIterator&
  operator++() {
    advance();
    return *this;
  }

  void
  operator++(int) {
    advance();
  }


  friend bool
  operator==(const PreorderIterator& it, std::default_sentinel_t) {
    return it.atEnd;
  }

private:
  void
  advance() {
    if (cursor.gotoFirstChild()) {
      return;
    }

    while (!cursor.gotoNextSibling()) {
      if (!cursor.gotoParent()) {
        atEnd = true;
        return;
      }
    }
  }

  Cursor cursor;
  bool atEnd = false;
};


class PreorderView : public std::ranges::view_interface<PreorderView> {
public:
  explicit PreorderView(Node root)
    : root{root}
      { }

  [[nodiscard]] PreorderIterator begin() const { return PreorderIterator{root}; }
  [[nodiscard]] std::default_sentinel_t end() const { return {}; }

private:
  Node root;
};


static_assert(std::input_iterator<PreorderIterator>);
static_assert(std::sentinel_for<std::default_sentinel_t, PreorderIterator>);
static_assert(std::ranges::input_range<PreorderView>);
static_assert(std::ranges::view<PreorderView>);
static_assert(std::copyable<PreorderView>);


[[nodiscard]] inline PreorderView
preorder(Node root) {
  return PreorderView{root};
}

}

#endif
