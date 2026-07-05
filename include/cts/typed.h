#ifndef CTS_TYPED_H
#define CTS_TYPED_H

// Typed dispatch over a tree. ts::typed builds an ordinary TreeVisitor out of
// per-node-type handlers. ts::typedFold does the same for value production.
// These can also take control over how their children will be visited using
// the Walk abstractions.


#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <cts/children.h>
#include <cts/common.h>
#include <cts/language.h>
#include <cts/node.h>
#include <cts/visitor.h>

namespace ts {

/////////////////////////////////////////////////////////////////////////////
// Handler descriptors.
// The values on/onLeave/otherwise produce, and the compile-time queries over
// a pack of them.
/////////////////////////////////////////////////////////////////////////////


namespace detail {

// Compile-time string usable as a template parameter, e.g. ts::on<"pair">.
template <size_t N>
struct FixedString {
  char data[N] = {};

  constexpr FixedString(char const (&str)[N]) {
    std::copy_n(str, N, data);
  }

  [[nodiscard]] constexpr std::string_view
  view() const {
    return std::string_view{data, N - 1};
  }
};


// Which traversal event a handler responds to.
enum class SpecKind : uint8_t { Enter, Leave, Otherwise };


// A handler descriptor combines a callable, the node type names that select
// it, and the traversal event they apply to. The callable's signature is
// checked by typed/typedFold rather than here, because the use case determines
// what it should be.
template <SpecKind Kind, typename F, FixedString... Names>
struct Spec {
  static constexpr std::array<std::string_view, sizeof...(Names)> names{
    Names.view()...};
  F callable;
};


template <SpecKind Kind, typename S>
inline constexpr bool isSpecKind = false;
template <SpecKind Kind, typename F, FixedString... Names>
inline constexpr bool isSpecKind<Kind, Spec<Kind, F, Names...>> = true;


template <typename S>
inline constexpr bool isEnterSpec = isSpecKind<SpecKind::Enter, S>;
template <typename S>
inline constexpr bool isLeaveSpec = isSpecKind<SpecKind::Leave, S>;
template <typename S>
inline constexpr bool isOtherwiseSpec = isSpecKind<SpecKind::Otherwise, S>;


template <typename... Ss>
inline constexpr size_t leaveCount =
  (0uz + ... + (isLeaveSpec<Ss> ? 1uz : 0uz));
template <typename... Ss>
inline constexpr size_t otherwiseCount =
  (0uz + ... + (isOtherwiseSpec<Ss> ? 1uz : 0uz));


// The name list a spec binds. otherwise binds no names.
template <typename S>
[[nodiscard]] constexpr std::span<std::string_view const>
specNames() {
  return {S::names};
}


// Index of the otherwise spec in a pack, or sizeof...(Ss) when absent.
template <typename... Ss>
[[nodiscard]] consteval size_t
otherwiseIndexOf() {
  constexpr std::array<bool, sizeof...(Ss)> flags{isOtherwiseSpec<Ss>...};
  for (size_t i = 0; i < flags.size(); ++i) {
    if (flags[i]) {
      return i;
    }
  }
  return sizeof...(Ss);
}


// The handlers for `leave` do not depend on what is doing the traversing, so
// onLeave checks it at the factory and gets a readable message out of it.
// Enter handlers do depend on the context, so on/otherwise defer to
// typed/typedFold.
template <typename F>
concept LeaveCallable =
  std::is_invocable_v<F&, Node>
  && std::is_void_v<std::invoke_result_t<F&, Node>>;

}


// Handler factories. A handler matches a node iff one of its strings
// equals node.getType() or names a supertype whose closure contains
// that type.

template <detail::FixedString... Names, typename F>
  requires (sizeof...(Names) > 0)
[[nodiscard]] constexpr auto
on(F callable) {
  return detail::Spec<detail::SpecKind::Enter, F, Names...>{
    std::move(callable)};
}


template <detail::FixedString... Names, typename F>
  requires (sizeof...(Names) > 0)
[[nodiscard]] constexpr auto
onLeave(F callable) {
  static_assert(detail::LeaveCallable<F>,
                "onLeave handlers take (ts::Node) and return void");
  return detail::Spec<detail::SpecKind::Leave, F, Names...>{
    std::move(callable)};
}


// The fallback handler. Be careful, a catch-anything lambda like
// [](auto&&...) binds as a walk handler and silently prunes every node it
// matches. Write (ts::Node) to mean "ignore the rest". Search for
// "walk-first" to find the documented concepts.
template <typename F>
[[nodiscard]] constexpr auto
otherwise(F callable) {
  return detail::Spec<detail::SpecKind::Otherwise, F>{std::move(callable)};
}


/////////////////////////////////////////////////////////////////////////////
// Walk proxies allow a visitor or folder to take control over how children
// will be traversed.
/////////////////////////////////////////////////////////////////////////////


// Dispatches the typed visitor over a child subtree on demand. Walk handlers
// own their descent, so the engine skips their children to allow them to
// explicitly visit their own children instead.
class Walk {
public:
  using RunFn = void (*)(void* self, Node child, bool& stopped);

  // Internal, constructed by TypedVisitor when dispatching a walk handler.
  Walk(void* self, RunFn run, bool* stopped)
    : self{self},
      run{run},
      stopped{stopped}
      { }

  Walk(const Walk&) = delete;
  Walk& operator=(const Walk&) = delete;

  // Dispatch the typed visitor over the subtree rooted at `child`.
  // No-op once the traversal has been stopped.
  void
  operator()(Node child) {
    if (!*stopped) {
      run(self, child, *stopped);
    }
  }

  // End the entire traversal. Remaining walk calls become no-ops and the
  // engine receives Stop.
  void
  stop() {
    *stopped = true;
  }

private:
  void* self;
  RunFn run;
  bool* stopped;
};


// Controls the traversal of children while visiting.
template <typename R>
class WalkFold {
public:
  using RunFn = R (*)(void* self, Node child);

  // Internal, constructed by TypedFolder when dispatching a handler.
  WalkFold(void* self, RunFn run)
    : self{self},
      run{run}
      { }

  WalkFold(const WalkFold&) = delete;
  WalkFold& operator=(const WalkFold&) = delete;

  [[nodiscard]] R
  operator()(Node child) {
    return run(self, child);
  }

private:
  void* self;
  RunFn run;
};


/////////////////////////////////////////////////////////////////////////////
// Handler signatures.
// The callable signatures typed and typedFold accept, and the pack-level
// rules they enforce on top of them.
/////////////////////////////////////////////////////////////////////////////


namespace detail {

// ts::typed and ts::typedFold each accept two handler signatures. One takes
// only the matched node, and the other also takes a walk proxy and is
// expected to descend into the node's children itself.
//
// Both choose walk-first, so a callable like [](auto&&...) would be bound to
// a walk handler. Writing the parameter list as (ts::Node) selects the other
// signature.

// The two signatures ts::typed accepts...
template <typename F>
concept VisitEnterCallable =
  std::is_invocable_v<F&, Node>
  && (std::is_same_v<std::invoke_result_t<F&, Node>, VisitAction>
      || std::is_void_v<std::invoke_result_t<F&, Node>>);

template <typename F>
concept VisitWalkCallable =
  std::is_invocable_v<F&, Node, Walk&>
  && std::is_void_v<std::invoke_result_t<F&, Node, Walk&>>;

template <typename F>
concept VisitCallable = VisitEnterCallable<F> || VisitWalkCallable<F>;

// ...and the two ts::typedFold accepts.
template <typename F, typename R>
concept FoldLeafCallable =
  std::is_invocable_v<F&, Node>
  && std::convertible_to<std::invoke_result_t<F&, Node>, R>;

template <typename F, typename R>
concept FoldWalkCallable =
  std::is_invocable_v<F&, Node, WalkFold<R>&>
  && std::convertible_to<std::invoke_result_t<F&, Node, WalkFold<R>&>, R>;

template <typename F, typename R>
concept FoldCallable = FoldLeafCallable<F, R> || FoldWalkCallable<F, R>;


// Whether one spec carries a callable ts::typed can dispatch.
template <typename S>
struct VisitSpecCheck : std::false_type {};
template <typename F, FixedString... Names>
struct VisitSpecCheck<Spec<SpecKind::Enter, F, Names...>>
  : std::bool_constant<VisitCallable<F>> {};
template <typename F, FixedString... Names>
struct VisitSpecCheck<Spec<SpecKind::Leave, F, Names...>>
  : std::bool_constant<LeaveCallable<F>> {};
template <typename F>
struct VisitSpecCheck<Spec<SpecKind::Otherwise, F>>
  : std::bool_constant<VisitCallable<F>> {};


// The same for ts::typedFold. Leave specs stay disabled for typedFold.
template <typename S, typename R>
struct FoldSpecCheck : std::false_type {};
template <typename F, FixedString... Names, typename R>
struct FoldSpecCheck<Spec<SpecKind::Enter, F, Names...>, R>
  : std::bool_constant<FoldCallable<F, R>> {};
template <typename F, typename R>
struct FoldSpecCheck<Spec<SpecKind::Otherwise, F>, R>
  : std::bool_constant<FoldCallable<F, R>> {};


template <typename... Ss>
concept VisitSpecPack =
  (VisitSpecCheck<Ss>::value && ...)
  && otherwiseCount<Ss...> <= 1;


// typedFold requires exactly one otherwise, because every node must yield an R.
template <typename R, typename... Ss>
concept FoldSpecPack =
  !std::is_void_v<R>
  && std::move_constructible<R>
  && (FoldSpecCheck<Ss, R>::value && ...)
  && otherwiseCount<Ss...> == 1;

}


/////////////////////////////////////////////////////////////////////////////
// Dispatch tables.
// A symbol-indexed table is build once when the visitor is constructed to
// make dispatch a single lookup per node. Handler names are resolved
// against the language during construction.
/////////////////////////////////////////////////////////////////////////////


namespace detail {

// A handler slot is a 1-based index into the spec pack. Index 0 means
// "no handler". The tables store slots and dispatch converts back to an index.
inline constexpr uint16_t noHandlerSlot = 0;

[[nodiscard]] constexpr uint16_t
slotForSpec(size_t specIndex) {
  return static_cast<uint16_t>(specIndex + 1);
}

[[nodiscard]] constexpr size_t
specIndexForSlot(uint16_t slot) {
  return static_cast<size_t>(slot) - 1;
}


template <typename... Ss>
inline constexpr bool specPackFitsInSlots =
  sizeof...(Ss) <= std::numeric_limits<uint16_t>::max();


// Names bind and resolve independently per event, so the tables, supertype
// binds, and ambiguity rules are all per-event.
enum class VisitEvent : bool { Enter, Leave };


// The resolved handlers for one visit event.
struct HandlerTable {
  std::vector<uint16_t> slots;  // indexed by symbol id
  // Error nodes have a synthetic error symbol, so they get a synthetic slot
  uint16_t errorSlot = noHandlerSlot;

  [[nodiscard]] uint16_t
  slotFor(Node node) const {
    size_t const symbol = std::to_underlying(node.getSymbol());
    if (symbol < slots.size()) {
      return slots[symbol];
    }

    return node.isError() ? errorSlot : noHandlerSlot;
  }
};


struct HandlerMaps {
  HandlerTable enter;
  HandlerTable leave;
};


// A type-erased view of one spec. An otherwise spec has an empty name list.
struct SpecBinding {
  std::span<std::string_view const> names;
  VisitEvent event = VisitEvent::Enter;
};


// Symbols reachable beneath one supertype.
struct SupertypeExpansion {
  std::vector<Symbol> concrete;  // symbols that can appear in trees
  std::vector<Symbol> nested;    // supertypes found on the way down
};


// Recursively expands `super` into `expansion`.
//
// `path` tracks the current recursion stack. Encountering a supertype already
// on the stack stops expansion, preventing infinite recursion from malformed
// grammars. This preserves the expansion for well-formed acyclic grammars,
// including duplicates from diamond hierarchies.
inline void
expandSupertypeAlongPath(Language language, Symbol super,
                         SupertypeExpansion& expansion,
                         std::vector<Symbol>& path) {
  path.push_back(super);
  for (Symbol const sub : language.getSubtypes(super)) {
    if (language.getSymbolType(sub) != SymbolType::Supertype) {
      expansion.concrete.push_back(sub);
      continue;
    }
    expansion.nested.push_back(sub);
    if (!std::ranges::contains(path, sub)) {
      expandSupertypeAlongPath(language, sub, expansion, path);
    }
  }
  path.pop_back();
}


// Everything beneath `super`.
// TODO: Expansion enumerates paths, so it is exponential in chained diamonds.
// Deduplicating would fix that and break the sort key, so a size cap is the
// easy defense to harden. Maybe consider improving this design in the future.
[[nodiscard]] inline SupertypeExpansion
expandSupertype(Language language, Symbol super) {
  SupertypeExpansion expansion;
  std::vector<Symbol> path;
  expandSupertypeAlongPath(language, super, expansion, path);
  return expansion;
}


// One handler's claim on one supertype name, plus what that name expands to.
struct SupertypeBind {
  uint16_t slot;
  Symbol super;
  VisitEvent event;
  SupertypeExpansion expansion;
};


// Resolves a handler names against a language, following the precedence
//     exact match -> nearest supertype -> otherwise
// Resolution first binds exact names and collects supertype claim/bindings
// and then places those claims around the exact bindings.
class HandlerMapResolver {
public:
  explicit HandlerMapResolver(Language language)
    : language{language},
      symbolCount{language.getNumSymbols()} {
    maps.enter.slots.assign(symbolCount, noHandlerSlot);
    maps.leave.slots.assign(symbolCount, noHandlerSlot);
    enterOwners.assign(symbolCount, Symbol::None);
    leaveOwners.assign(symbolCount, Symbol::None);
  }

  [[nodiscard]] std::expected<HandlerMaps, Error>
  resolve(std::span<SpecBinding const> specs) {
    for (size_t i = 0; i < specs.size(); ++i) {
      if (auto bound = bindSpec(specs[i], slotForSpec(i)); !bound) {
        return std::unexpected{bound.error()};
      }
    }
    if (auto applied = applySupertypeBinds(); !applied) {
      return std::unexpected{applied.error()};
    }
    return std::move(maps);
  }

private:
  static constexpr std::string_view errorTypeName = "ERROR";

  // A name can resolve to a real supertype symbol whose subtype is
  // runtime-invisible (with older grammars) and does not expand to concrete
  // symbols. We can report those names that can never fire instead of
  // registering them.
  enum class SupertypeClaim : uint8_t { Dispatchable, NeverMatches };

  // One handler claiming one name for one visit event.
  struct NameClaim {
    std::string_view name;
    VisitEvent event;
    uint16_t slot;
  };

  // Pass 1) Bind the names a spec lists with their symbols and collect their
  // supertype names.
  [[nodiscard]] std::expected<void, Error>
  bindSpec(SpecBinding const& spec, uint16_t slot) {
    for (std::string_view const name : spec.names) {
      NameClaim const claim{
        .name = name, .event = spec.event, .slot = slot};
      if (auto bound = bindName(claim); !bound) {
        return bound;
      }
    }
    return {};
  }

  // Binds one of a spec's names. More than one symbol can carry the same name,
  // e.g. an alias, so bind every match rather than the first.
  [[nodiscard]] std::expected<void, Error>
  bindName(NameClaim claim) {
    if (claim.name == errorTypeName) {
      return bindErrorSlot(claim);
    }

    bool dispatchable = false;
    for (size_t index = 0; index < symbolCount; ++index) {
      Symbol const symbol{static_cast<uint16_t>(index)};
      if (language.getSymbolName(symbol) != claim.name) {
        continue;
      }
      switch (language.getSymbolType(symbol)) {
        case SymbolType::Regular:
        case SymbolType::Anonymous: {
          if (auto bound = bindConcreteSymbol(symbol, claim); !bound) {
            return bound;
          }
          dispatchable = true;
          break;
        }
        case SymbolType::Supertype: {
          auto const claimed = claimSupertype(symbol, claim);
          if (!claimed) {
            return std::unexpected{claimed.error()};
          }
          if (*claimed == SupertypeClaim::Dispatchable) {
            dispatchable = true;
          }
          break;
        }
        case SymbolType::Auxiliary:
          break;  // internal bookkeeping symbols never appear in trees
      }
    }

    if (!dispatchable) {
      return std::unexpected{
        Error{.kind = ErrorKind::VisitorNodeType, .name = claim.name}};
    }
    return {};
  }

  // Matches error nodes by name, like the query language's (ERROR).
  [[nodiscard]] std::expected<void, Error>
  bindErrorSlot(NameClaim claim) {
    uint16_t& errorSlot = tableFor(claim.event).errorSlot;
    if (errorSlot != noHandlerSlot && errorSlot != claim.slot) {
      return duplicateHandlerError(claim.name);
    }
    errorSlot = claim.slot;
    return {};
  }

  // A distinct second handler claiming a symbol is a duplicate.
  [[nodiscard]] std::expected<void, Error>
  bindConcreteSymbol(Symbol symbol, NameClaim claim) {
    uint16_t& bound = tableFor(claim.event).slots[std::to_underlying(symbol)];
    if (bound != noHandlerSlot && bound != claim.slot) {
      return duplicateHandlerError(claim.name);
    }
    bound = claim.slot;
    return {};
  }

  // Record a supertype name expanded to the symbols it covers.
  [[nodiscard]] std::expected<SupertypeClaim, Error>
  claimSupertype(Symbol super, NameClaim claim) {
    SupertypeExpansion expansion = expandSupertype(language, super);
    if (expansion.concrete.empty()) {
      return SupertypeClaim::NeverMatches;
    }
    for (SupertypeBind const& existing : superBinds) {
      bool const claimedByOtherHandler = existing.super == super
                                      && existing.event == claim.event
                                      && existing.slot != claim.slot;
      if (claimedByOtherHandler) {
        return duplicateHandlerError(claim.name);
      }
    }
    superBinds.push_back(SupertypeBind{.slot = claim.slot,
                                       .super = super,
                                       .event = claim.event,
                                       .expansion = std::move(expansion)});
    return SupertypeClaim::Dispatchable;
  }

  // Pass 2) Apply supertype bindings.
  // Exact bindings take precedence. When multiple supertypes match, the
  // innermost one wins, and overlapping minimal supertypes are ambiguous.
  //
  // Sorting by increasing expansion size processes nested supertypes before
  // enclosing ones. In an acyclic supertype graph, every enclosing expansion
  // strictly contains the nested expansion (and the nested supertype itself),
  // so the first bind to claim a symbol is always one of its minimal matches.
  [[nodiscard]] std::expected<void, Error>
  applySupertypeBinds() {
    std::ranges::stable_sort(superBinds, {}, [](SupertypeBind const& bind) {
      return bind.expansion.nested.size();
    });
    for (SupertypeBind const& bind : superBinds) {
      for (Symbol const symbol : bind.expansion.concrete) {
        if (auto placed = placeSupertypeBind(bind, symbol); !placed) {
          return placed;
        }
      }
    }
    return {};
  }

  [[nodiscard]] std::expected<void, Error>
  placeSupertypeBind(SupertypeBind const& bind, Symbol symbol) {
    size_t const index = std::to_underlying(symbol);
    uint16_t& boundSlot = tableFor(bind.event).slots[index];
    Symbol& owner = ownersFor(bind.event)[index];

    // An exact name already claimed the symbol, and wins.
    if (boundSlot != noHandlerSlot && owner == Symbol::None) {
      return {};
    }
    // This handler covers the symbol already via a repeated or nested name.
    if (boundSlot == bind.slot) {
      return {};
    }
    if (boundSlot == noHandlerSlot) {
      boundSlot = bind.slot;
      owner = bind.super;
      return {};
    }
    if (ownerIsNearer(bind, symbol, owner, boundSlot)) {
      return {};
    }
    // Report a contested concrete type rather than either supertype.
    return std::unexpected{
      Error{.kind = ErrorKind::VisitorSupertypeAmbiguity,
            .name =
              language.getSymbolName(symbol).value_or(std::string_view{})}};
  }

  // Returns whether the existing owner remains the nearer match for `symbol`.
  // Binds are processed innermost-first, so `bind` cannot be nested inside the
  // current owner. The owner wins if it is nested inside `bind`, or if another
  // bind from the same handler is nested inside `bind` and also covers `symbol`.
  // Otherwise the overlap is ambiguous.
  [[nodiscard]] bool
  ownerIsNearer(SupertypeBind const& bind, Symbol symbol, Symbol ownerSuper,
                uint16_t ownerSlot) const {
    if (std::ranges::contains(bind.expansion.nested, ownerSuper)) {
      return true;
    }
    return std::ranges::any_of(superBinds, [&](SupertypeBind const& other) {
      return other.slot == ownerSlot
          && other.event == bind.event
          && std::ranges::contains(bind.expansion.nested, other.super)
          && std::ranges::contains(other.expansion.concrete, symbol);
    });
  }

  [[nodiscard]] static std::unexpected<Error>
  duplicateHandlerError(std::string_view name) {
    return std::unexpected{
      Error{.kind = ErrorKind::VisitorDuplicate, .name = name}};
  }

  [[nodiscard]] HandlerTable&
  tableFor(VisitEvent event) {
    return event == VisitEvent::Leave ? maps.leave : maps.enter;
  }

  [[nodiscard]] std::vector<Symbol>&
  ownersFor(VisitEvent event) {
    return event == VisitEvent::Leave ? leaveOwners : enterOwners;
  }

  Language language;
  size_t symbolCount;
  HandlerMaps maps;
  std::vector<SupertypeBind> superBinds;
  // Per symbol maps to the supertype that determined a Symbol's handler
  // (or Symbol::None)
  std::vector<Symbol> enterOwners;
  std::vector<Symbol> leaveOwners;
};


[[nodiscard]] inline std::expected<HandlerMaps, Error>
resolveHandlerMaps(Language language, std::span<SpecBinding const> specs) {
  return HandlerMapResolver{language}.resolve(specs);
}


// Shared helper for typed/typedFold that describes each spec and resolves
// the handler maps against the language.
template <typename... Specs>
[[nodiscard]] std::expected<HandlerMaps, Error>
resolveMapsFor(Language language) {
  std::array<SpecBinding, sizeof...(Specs)> const bindings{
    SpecBinding{.names = specNames<Specs>(),
                .event = isLeaveSpec<Specs> ? VisitEvent::Leave
                                            : VisitEvent::Enter}...};
  return resolveHandlerMaps(language, bindings);
}


// Dispatches to callback(std::integral_constant<size_t, index>{}) for the
// selected compile-time index. Callers guarantee index < N.
// The dispatch is fully unrolled so the selected callback can be inlined.
// `Result` is explicit to keep the empty-pack case well-formed.
template <size_t N, typename Result, typename F>
Result
dispatchAt(size_t index, F&& callback) {
  auto const atMatchingIndex = [&](auto&& consume) {
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      (void)((Is == index
                ? (consume(std::integral_constant<size_t, Is>{}), true)
                : false)
             || ...);
    }(std::make_index_sequence<N>{});
  };

  if constexpr (std::is_void_v<Result>) {
    atMatchingIndex(callback);

  } else {
    std::optional<Result> result;
    atMatchingIndex([&](auto matched) { result.emplace(callback(matched)); });
    return std::move(*result);
  }
}

}


/////////////////////////////////////////////////////////////////////////////
// Typed visiting.
// TypedVisitor and its ts::typed validating factory.
/////////////////////////////////////////////////////////////////////////////


// A TreeVisitor that dispatches per node type. Obtain instances from
// ts::typed, which validates handler names against the language. It runs
// using the ordinary engine with ts::visit(root, *visitor).
template <typename... Specs>
class TypedVisitor {
  static_assert(detail::specPackFitsInSlots<Specs...>,
                "too many handlers to index with a uint16_t slot");

  template <size_t I>
  using SpecAt = std::tuple_element_t<I, std::tuple<Specs...>>;

  static constexpr size_t otherwiseIndex =
    detail::otherwiseIndexOf<Specs...>();
  static constexpr bool hasOtherwise = otherwiseIndex < sizeof...(Specs);
  static constexpr bool hasLeaveHandlers = detail::leaveCount<Specs...> > 0;

public:
  // Internal, prefer to use ts::typed.
  TypedVisitor(std::tuple<Specs...> specsIn, detail::HandlerMaps mapsIn)
    : specs{std::move(specsIn)},
      maps{std::move(mapsIn)}
      { }

  [[nodiscard]] VisitAction
  onEnter(Node node) {
    bool stopped = false;
    return enterNode(node, stopped);
  }

  void
  onLeave(Node node) requires (hasLeaveHandlers) {
    leaveNode(node);
  }

private:
  VisitAction
  enterNode(Node node, bool& stopped) {
    if (stopped) {
      return VisitAction::Stop;
    }

    uint16_t const slot = maps.enter.slotFor(node);
    if (slot != detail::noHandlerSlot) {
      return dispatchEnter(detail::specIndexForSlot(slot), node, stopped);
    }

    if constexpr (hasOtherwise) {
      return dispatchEnter(otherwiseIndex, node, stopped);

    } else {
      return VisitAction::Continue;
    }
  }

  void
  leaveNode(Node node) {
    if constexpr (hasLeaveHandlers) {
      uint16_t const slot = maps.leave.slotFor(node);
      if (slot != detail::noHandlerSlot) {
        dispatchLeave(detail::specIndexForSlot(slot), node);
      }

    } else {
      // no leave handlers found, so there is nothing to look up
    }
  }

  VisitAction
  dispatchEnter(size_t index, Node node, bool& stopped) {
    return detail::dispatchAt<sizeof...(Specs), VisitAction>(
      index, [&]<size_t I>(std::integral_constant<size_t, I>) {
        return this->template invokeEnter<I>(node, stopped);
      });
  }

  template <size_t I>
  VisitAction
  invokeEnter(Node node, bool& stopped) {
    if constexpr (detail::isLeaveSpec<SpecAt<I>>) {
      // Leave specs should never reach this point by design.
      std::unreachable();

    } else {
      return invokeEnterCallable(std::get<I>(specs).callable, node, stopped);
    }
  }

  // One handler call, dispatched on the signature the handler declared.
  template <typename F>
  VisitAction
  invokeEnterCallable(F& callable, Node node, bool& stopped) {
    if constexpr (detail::VisitWalkCallable<F>) {
      // Walking handlers are matched first, so a signature that might match
      // both will default to walking. The walk takes ownership over
      // traversal, which is why it must SkipChildren.
      Walk walk = subtreeWalker(stopped);
      callable(node, walk);
      return stopped ? VisitAction::Stop : VisitAction::SkipChildren;

    } else if constexpr (std::is_void_v<std::invoke_result_t<F&, Node>>) {
      callable(node);
      return VisitAction::Continue;

    } else {
      VisitAction const action = callable(node);
      if (action == VisitAction::Stop) {
        stopped = true;
      }
      return action;
    }
  }

  void
  dispatchLeave(size_t index, Node node) {
    detail::dispatchAt<sizeof...(Specs), void>(
      index, [&]<size_t I>(std::integral_constant<size_t, I>) {
        this->template invokeLeave<I>(node);
      });
  }

  template <size_t I>
  void
  invokeLeave(Node node) {
    if constexpr (detail::isLeaveSpec<SpecAt<I>>) {
      std::get<I>(specs).callable(node);

    } else {
      std::unreachable();  // only leave specs land in the leave table
    }
  }

  [[nodiscard]] Walk
  subtreeWalker(bool& stopped) {
    return Walk{this,
                [](void* self, Node child, bool& walkStopped) {
                  static_cast<TypedVisitor*>(self)->runSubtree(child,
                                                               walkStopped);
                },
                &stopped};
  }

  // Presents the typed dispatch to the engine while sharing the walk's stop
  // flag. Sharing it lets a stop reached deep inside a walked subtree end
  // the enclosing traversal too.
  struct SubtreeVisitor {
    TypedVisitor* visitor;
    bool* stopped;

    VisitAction
    onEnter(Node node) {
      VisitAction const action = visitor->enterNode(node, *stopped);
      if (*stopped || action == VisitAction::Stop) {
        *stopped = true;
        return VisitAction::Stop;
      }
      return action;
    }

    // Only define when leave handlers are present to delegate to.
    void
    onLeave(Node node) requires (hasLeaveHandlers) {
      visitor->leaveNode(node);
    }
  };

  // Typed traversal of one subtree on behalf of Walk. It runs on the same
  // iterative engine as a plain visit, so its stack cost does not grow with
  // the depth of the subtree until a typed handler must be invoked.
  void
  runSubtree(Node node, bool& stopped) {
    if (stopped) {
      return;
    }
    ts::visit(node, SubtreeVisitor{.visitor = this, .stopped = &stopped});
  }

  std::tuple<Specs...> specs;
  detail::HandlerMaps maps;
};


// Builds a typed visitor over `language`. Fails when a handler names an
// unknown type, when two handlers claim one type, or when a type's
// nearest supertype handlers are ambiguous (more than one handler).
template <typename... Specs>
  requires detail::VisitSpecPack<std::remove_cvref_t<Specs>...>
[[nodiscard]] std::expected<TypedVisitor<std::remove_cvref_t<Specs>...>,
                            Error>
typed(Language language, Specs&&... specs) {
  auto maps =
    detail::resolveMapsFor<std::remove_cvref_t<Specs>...>(language);
  if (!maps) {
    return std::unexpected{maps.error()};
  }
  return TypedVisitor<std::remove_cvref_t<Specs>...>{
    std::tuple<std::remove_cvref_t<Specs>...>{std::forward<Specs>(specs)...},
    std::move(*maps)};
}


/////////////////////////////////////////////////////////////////////////////
// Typed folding.
// TypedFolder and its ts::typedFold validating factory.
/////////////////////////////////////////////////////////////////////////////


// A value-returning typed traversal where every node produces an R and
// handlers can pull child results on demand via WalkFold. Create instances
// with ts::typedFold. The runner is recursive and consumes the stack.
template <typename R, typename... Specs>
class TypedFolder {
  static_assert(detail::specPackFitsInSlots<Specs...>,
                "too many handlers to index with a uint16_t slot");

  static constexpr size_t otherwiseIndex =
    detail::otherwiseIndexOf<Specs...>();

public:
  // Internal, use ts::typedFold.
  TypedFolder(std::tuple<Specs...> specsIn, detail::HandlerMaps mapsIn)
    : specs{std::move(specsIn)},
      maps{std::move(mapsIn)}
      { }

  [[nodiscard]] R
  run(Node root) {
    return runNode(root);
  }

private:
  R
  runNode(Node node) {
    // typedFold requires an otherwise, so every node has a handler.
    uint16_t const slot = maps.enter.slotFor(node);
    size_t const index = slot != detail::noHandlerSlot
                       ? detail::specIndexForSlot(slot)
                       : otherwiseIndex;
    return dispatchFold(index, node);
  }

  R
  dispatchFold(size_t index, Node node) {
    return detail::dispatchAt<sizeof...(Specs), R>(
      index, [&]<size_t I>(std::integral_constant<size_t, I>) {
        return this->template invokeFold<I>(node);
      });
  }

  template <size_t I>
  R
  invokeFold(Node node) {
    return invokeFoldCallable(std::get<I>(specs).callable, node);
  }

  // One handler call, dispatched on the signature the handler declared.
  template <typename F>
  R
  invokeFoldCallable(F& callable, Node node) {
    if constexpr (detail::FoldWalkCallable<F, R>) {
      // Walk-first, so a callable matching both signatures lands here and is
      // responsible for pulling child results (see the note above the
      // handler-signature concepts).
      WalkFold<R> walk = childFolder();
      return callable(node, walk);
    } else {
      return callable(node);
    }
  }

  [[nodiscard]] WalkFold<R>
  childFolder() {
    return WalkFold<R>{this, [](void* self, Node child) -> R {
      return static_cast<TypedFolder*>(self)->runNode(child);
    }};
  }

  std::tuple<Specs...> specs;
  detail::HandlerMaps maps;
};


// Builds a typed folder over `language`. Construction validates handler names
// like ts::typed. Requires value-returning handlers, no onLeave, and one
// otherwise handler.
template <typename R, typename... Specs>
  requires detail::FoldSpecPack<R, std::remove_cvref_t<Specs>...>
[[nodiscard]] std::expected<TypedFolder<R, std::remove_cvref_t<Specs>...>,
                            Error>
typedFold(Language language, Specs&&... specs) {
  auto maps =
    detail::resolveMapsFor<std::remove_cvref_t<Specs>...>(language);
  if (!maps) {
    return std::unexpected{maps.error()};
  }
  return TypedFolder<R, std::remove_cvref_t<Specs>...>{
    std::tuple<std::remove_cvref_t<Specs>...>{std::forward<Specs>(specs)...},
    std::move(*maps)};
}

}

#endif
