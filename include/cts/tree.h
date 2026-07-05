#ifndef CTS_TREE_H
#define CTS_TREE_H

#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

#include <tree_sitter/api.h>

#include <cts/node.h>

namespace ts {

class Tree {
public:
  // Precondition: tree is non-null. Trees are created by Parser::parse,
  // which never hands a null pointer through.
  explicit Tree(TSTree* tree)
    : impl{tree, ts_tree_delete} {
    assert(tree != nullptr);
  }

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


  // Record a source edit so a subsequent parse(source, *this) can reuse
  // unchanged subtrees. Nodes fetched before the edit will be stale
  // and should be refetched.
  void
  edit(const InputEdit& inputEdit) {
    TSInputEdit const raw = inputEdit.toRaw();
    ts_tree_edit(impl.get(), &raw);
  }


  [[nodiscard]] std::vector<Range>
  getChangedRanges(const Tree& newer) const {
    uint32_t count = 0;
    std::unique_ptr<TSRange, FreeHelper> const ranges{
      ts_tree_get_changed_ranges(impl.get(), newer.impl.get(), &count)};
    std::vector<Range> result;
    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      result.push_back(detail::toRange(ranges.get()[i]));
    }
    return result;
  }


  // Escape hatch to the C API. The Tree retains ownership.
  [[nodiscard]] TSTree const* raw() const { return impl.get(); }
  [[nodiscard]] TSTree* raw() { return impl.get(); }


  // Writes a Graphviz DOT rendering of the tree. Unlike the Parser variant,
  // this does *not* take ownership over file.
  void
  printDotGraph(std::FILE* file) const {
    if (file == nullptr) {
      return;
    }
    ts_tree_print_dot_graph(impl.get(), fileno(file));
  }

private:
  std::unique_ptr<TSTree, decltype(&ts_tree_delete)> impl;
};

}

#endif
