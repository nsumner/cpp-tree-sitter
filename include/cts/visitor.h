#ifndef CTS_VISITOR_H
#define CTS_VISITOR_H

#include <concepts>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <cts/cursor.h>
#include <cts/node.h>

namespace ts {

enum class VisitAction : uint8_t {
  Continue,      // descend into this node's children
  SkipChildren,  // skip the children and instead go to the next sibling
  Stop,          // end the entire traversal
};


// A TreeVisitor is anything with an onEnter(Node) method that returns a
// VisitAction to control the traversal. onEnter() is invoked when visiting
// a Node in-order, and the VisitAction determines whether children will be
// visited and whether traversal continues.
template <typename V>
concept TreeVisitor = requires(V visitor, Node node) {
  { visitor.onEnter(node) } -> std::same_as<VisitAction>;
};


// A LeavingTreeVisitor is a TreeVisitor that also has onLeave(Node), which
// is invoked after traversing a Node's children.
template <typename V>
concept LeavingTreeVisitor =
  TreeVisitor<V> && requires(V visitor, Node node) {
    { visitor.onLeave(node) };
  };


// Iterative cursor-based traversal to visit the subtree rooted at `root`.
// onLeave (when present) fires for every node whose onEnter returned
// Continue or SkipChildren unless the walk was stopped first. The order is
// LIFO as if recursive.
template <TreeVisitor V>
void
visit(Node root, V&& visitor) {
  Cursor cursor = root.getCursor();
  while (true) {
    VisitAction const action = visitor.onEnter(cursor.getCurrentNode());
    if (action == VisitAction::Stop) {
      return;
    }
    if (action != VisitAction::SkipChildren && cursor.gotoFirstChild()) {
      continue;
    }
    while (true) {
      if constexpr (LeavingTreeVisitor<std::remove_cvref_t<V>>) {
        visitor.onLeave(cursor.getCurrentNode());
      }
      if (cursor.gotoNextSibling()) {
        break;
      }
      if (!cursor.gotoParent()) {
        return;  // climbed back to (and left) the root
      }
    }
  }
}


// A TreeFolder builds a new value based on a Node and a list of values for
// the Node's children. This is used by fold() to perform a fold over the
// tree. This is an alternative to a visitor when the main point is
// generating a new value.
template <typename F, typename R>
concept TreeFolder = requires(F folder, Node node, std::span<R> childResults) {
  { folder.onNode(node, childResults) } -> std::convertible_to<R>;
};


// Post-order fold over the subtree rooted at `root`. Each node's result is
// computed from its children's results. Leaves receive an empty span. Unlike
// visit, fold allocates to maintain a stack of child-result frames.
template <typename R, TreeFolder<R> F>
[[nodiscard]] R
fold(Node root, F&& folder) {
  Cursor cursor = root.getCursor();
  std::vector<std::vector<R>> frames;
  frames.emplace_back();  // reserve space for the root

  while (true) {
    if (cursor.gotoFirstChild()) {
      frames.emplace_back();  // reserve space for the child
      continue;
    }

    // Add in trivial results for leaves
    frames.back().push_back(folder.onNode(cursor.getCurrentNode(),
                                          std::span<R>{}));

    while (!cursor.gotoNextSibling()) {
      if (!cursor.gotoParent()) {
        return std::move(frames.back().back());
      }
      // Fold the node itself after all the children are folded in.
      std::vector<R> childResults = std::move(frames.back());
      frames.pop_back();
      frames.back().push_back(folder.onNode(cursor.getCurrentNode(),
                                            std::span<R>{childResults}));
    }
  }
}

}

#endif
