//===- CIRNullabilitySafety.cpp - Flow-sensitive nullability on CIR -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The lattice maps access paths to a nullness state. A path is a non-escaping
// local slot (a cir.alloca whose address is only loaded from, stored to or
// projected with cir.get_member) followed by member and dereference steps, so
// `p`, `s.ptr` and `p->next` each get a path. A path missing from the map has
// no flow fact and falls back to the declared nullability; a join keeps only
// the paths present on both sides. Stores through memory that is not a
// tracked path, and calls, drop every fact reached through a dereference.
// A pointer that was dereferenced is non-null afterwards, so each pointer
// warns once.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/Analysis/CIRNullabilitySafety.h"
#include "mlir/Analysis/CallGraph.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/SymbolTable.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SmallVector.h"
#include <functional>
#include <map>

using namespace mlir;
using namespace mlir::dataflow;

namespace cir {
namespace {

enum class NullState : uint8_t { NonNull, Null, Unknown, Nullable };

NullState joinStates(NullState a, NullState b) {
  if (a == b)
    return a;
  if (a == NullState::Nullable || b == NullState::Nullable)
    return NullState::Nullable;
  if ((a == NullState::NonNull && b == NullState::Unknown) ||
      (a == NullState::Unknown && b == NullState::NonNull))
    return NullState::Unknown;
  return NullState::Nullable;
}

bool mayBeNull(NullState s) {
  return s == NullState::Null || s == NullState::Nullable;
}

constexpr unsigned derefStep = ~0u;

struct AccessPath {
  Value root;
  SmallVector<unsigned, 2> steps;

  bool operator<(const AccessPath &other) const {
    if (root != other.root)
      return root.getAsOpaquePointer() < other.root.getAsOpaquePointer();
    return steps < other.steps;
  }
  bool throughPointer() const { return llvm::is_contained(steps, derefStep); }
  bool extends(const AccessPath &prefix) const {
    return root == prefix.root && steps.size() >= prefix.steps.size() &&
           std::equal(prefix.steps.begin(), prefix.steps.end(), steps.begin());
  }
};

using PathFacts = SmallVector<std::pair<AccessPath, NullState>, 2>;

struct Guard {
  PathFacts onTrue;
  PathFacts onFalse;

  static bool same(const PathFacts &a, const PathFacts &b) {
    if (a.size() != b.size())
      return false;
    for (auto [x, y] : llvm::zip(a, b))
      if (x.first < y.first || y.first < x.first || x.second != y.second)
        return false;
    return true;
  }
  bool operator==(const Guard &other) const {
    return same(onTrue, other.onTrue) && same(onFalse, other.onFalse);
  }
  bool mentions(llvm::function_ref<bool(const AccessPath &)> pred) const {
    for (const PathFacts *facts : {&onTrue, &onFalse})
      for (const auto &fact : *facts)
        if (pred(fact.first))
          return true;
    return false;
  }
};

class NullLattice : public AbstractDenseLattice {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(NullLattice)
  using AbstractDenseLattice::AbstractDenseLattice;

  bool reached = false;
  std::map<AccessPath, NullState> facts;
  std::map<AccessPath, Guard> guards;
  std::map<AccessPath, Value> contents;

  ChangeResult join(const AbstractDenseLattice &rhs) override {
    const auto &other = static_cast<const NullLattice &>(rhs);
    if (!other.reached)
      return ChangeResult::NoChange;
    ChangeResult result = ChangeResult::NoChange;
    if (!reached) {
      reached = true;
      facts = other.facts;
      guards = other.guards;
      contents = other.contents;
      return ChangeResult::Change;
    }
    for (auto it = contents.begin(); it != contents.end();) {
      auto found = other.contents.find(it->first);
      if (found == other.contents.end() || found->second != it->second) {
        it = contents.erase(it);
        result = ChangeResult::Change;
        continue;
      }
      ++it;
    }
    for (auto it = guards.begin(); it != guards.end();) {
      auto found = other.guards.find(it->first);
      if (found == other.guards.end() || !(found->second == it->second)) {
        it = guards.erase(it);
        result = ChangeResult::Change;
        continue;
      }
      ++it;
    }
    for (auto it = facts.begin(); it != facts.end();) {
      auto found = other.facts.find(it->first);
      if (found == other.facts.end()) {
        it = facts.erase(it);
        result = ChangeResult::Change;
        continue;
      }
      NullState joined = joinStates(it->second, found->second);
      if (joined != it->second) {
        it->second = joined;
        result = ChangeResult::Change;
      }
      ++it;
    }
    return result;
  }

  ChangeResult enter() {
    if (reached)
      return ChangeResult::NoChange;
    reached = true;
    return ChangeResult::Change;
  }

  void print(raw_ostream &os) const override {
    if (!reached) {
      os << "<unreached>";
      return;
    }
    os << "{";
    for (const auto &[path, state] : facts) {
      os << " " << path.root.getAsOpaquePointer();
      for (unsigned step : path.steps)
        os << (step == derefStep ? "->" : ".")
           << (step == derefStep ? 0 : step);
      os << "=" << static_cast<int>(state);
    }
    os << " }";
  }
};

using LatticeLookup = std::function<const NullLattice *(Operation *)>;

struct ConditionFacts {
  PathFacts paths;
  SmallVector<std::pair<Value, NullState>> values;
};

class Semantics {
public:
  Semantics(const NullabilitySafetyCIROptions &options,
            const llvm::DenseSet<Value> &tracked,
            const llvm::DenseSet<Value> &escaped,
            const llvm::DenseSet<Operation *> &allReturnsNonnull,
            mlir::SymbolTableCollection &symbols)
      : options(options), tracked(tracked), escaped(escaped),
        allReturnsNonnull(allReturnsNonnull), symbols(symbols) {}

  std::optional<NullState> fromAttr(Attribute attr) const {
    auto kind = mlir::dyn_cast_if_present<cir::NullabilityAttr>(attr);
    if (!kind)
      return std::nullopt;
    switch (kind.getValue()) {
    case cir::Nullability::NonNull:
      return NullState::NonNull;
    case cir::Nullability::Nullable:
      return NullState::Nullable;
    case cir::Nullability::Unspecified:
      return options.unspecifiedIsNullable ? NullState::Nullable
                                           : NullState::Unknown;
    }
    return std::nullopt;
  }

  static bool isExplicit(Attribute attr) {
    auto kind = mlir::dyn_cast_if_present<cir::NullabilityAttr>(attr);
    return kind && kind.getValue() != cir::Nullability::Unspecified;
  }

  std::optional<NullState> declared(Operation *op) const {
    return fromAttr(op->getAttr(cir::CIRDialect::getNullabilityAttrName()));
  }

  std::optional<AccessPath> pathOfAddress(Value addr) const {
    if (tracked.contains(addr))
      return AccessPath{addr, {}};
    auto member = addr.getDefiningOp<cir::GetMemberOp>();
    if (!member) {
      if (std::optional<AccessPath> path = pathOfValue(addr)) {
        path->steps.push_back(derefStep);
        return path;
      }
      return std::nullopt;
    }
    Value base = member.getAddr();
    if (std::optional<AccessPath> path = pathOfAddress(base)) {
      path->steps.push_back(member.getIndex());
      return path;
    }
    if (std::optional<AccessPath> path = pathOfValue(base)) {
      path->steps.push_back(derefStep);
      path->steps.push_back(member.getIndex());
      return path;
    }
    return std::nullopt;
  }

  std::optional<AccessPath> pathOfValue(Value value) const {
    value = stripCasts(value);
    if (auto load = value.getDefiningOp<cir::LoadOp>())
      return pathOfAddress(load.getAddr());
    if (auto call = value.getDefiningOp<cir::CallOp>())
      if (smartPtrOp(call) == cir::SmartPtrOp::Access)
        return smartObject(call);
    return std::nullopt;
  }

  static Value stripCasts(Value value) {
    while (auto cast = value.getDefiningOp<cir::CastOp>()) {
      switch (cast.getKind()) {
      case cir::CastKind::bitcast:
      case cir::CastKind::address_space:
        value = cast.getSrc();
        continue;
      default:
        return value;
      }
    }
    return value;
  }

  static bool isNullConstant(Value value) {
    auto constant = stripCasts(value).getDefiningOp<cir::ConstantOp>();
    if (!constant)
      return false;
    if (auto ptr = mlir::dyn_cast<cir::ConstPtrAttr>(constant.getValue()))
      return ptr.isNullValue();
    if (auto integer = mlir::dyn_cast<cir::IntAttr>(constant.getValue()))
      return integer.getValue().isZero();
    return false;
  }

  NullState valueState(Value value, const LatticeLookup &lookup,
                       unsigned depth = 0) const {
    if (depth > 16)
      return NullState::Unknown;
    Operation *def = value.getDefiningOp();
    if (!def) {
      auto arg = mlir::cast<BlockArgument>(value);
      if (auto func =
              mlir::dyn_cast<cir::FuncOp>(arg.getOwner()->getParentOp()))
        if (arg.getOwner()->isEntryBlock())
          if (std::optional<NullState> state = fromAttr(
                  func.getArgAttr(arg.getArgNumber(),
                                  cir::CIRDialect::getNullabilityAttrName())))
            return *state;
      return NullState::Unknown;
    }
    if (fromAttr(def->getAttr(cir::CIRDialect::getNullabilityCastAttrName())) ==
            NullState::NonNull &&
        !isNullConstant(value))
      return NullState::NonNull;
    if (auto load = mlir::dyn_cast<cir::LoadOp>(def)) {
      if (std::optional<AccessPath> path = pathOfAddress(load.getAddr()))
        if (const NullLattice *before = lookup(load))
          if (auto found = before->facts.find(*path);
              found != before->facts.end())
            return found->second;
      if (std::optional<NullState> state = declared(load))
        return *state;
      if (auto alloca = load.getAddr().getDefiningOp<cir::AllocaOp>())
        if (std::optional<NullState> state = declared(alloca))
          return *state;
      return NullState::Unknown;
    }
    if (auto call = mlir::dyn_cast<cir::CallOp>(def)) {
      if (smartPtrOp(call) == cir::SmartPtrOp::Access) {
        std::optional<AccessPath> object = smartObject(call);
        const NullLattice *before = lookup(call);
        if (object && before)
          if (std::optional<NullState> state = pathState(*object, *before))
            return *state;
        return NullState::Unknown;
      }
      cir::FuncOp target = callee(call);
      if (target && allReturnsNonnull.contains(target) &&
          !isExplicit(call->getAttr(cir::CIRDialect::getNullabilityAttrName())))
        return NullState::NonNull;
      if (std::optional<NullState> state = declared(call))
        return *state;
      if (target)
        if (std::optional<NullState> state = fromAttr(target.getResultAttr(
                0, cir::CIRDialect::getNullabilityAttrName())))
          return *state;
      return NullState::Unknown;
    }
    if (auto constant = mlir::dyn_cast<cir::ConstantOp>(def)) {
      if (auto ptr = mlir::dyn_cast<cir::ConstPtrAttr>(constant.getValue()))
        return ptr.isNullValue() ? NullState::Null : NullState::NonNull;
      return NullState::Unknown;
    }
    if (mlir::isa<cir::AllocaOp, cir::GetGlobalOp, cir::GetMemberOp>(def))
      return NullState::NonNull;
    if (auto dynCast = mlir::dyn_cast<cir::DynamicCastOp>(def))
      return dynCast.isRefCast() ? NullState::NonNull : NullState::Nullable;
    if (auto stride = mlir::dyn_cast<cir::PtrStrideOp>(def))
      return valueState(stride.getBase(), lookup, depth + 1);
    if (auto cast = mlir::dyn_cast<cir::CastOp>(def)) {
      switch (cast.getKind()) {
      case cir::CastKind::bitcast:
      case cir::CastKind::address_space:
        return valueState(cast.getSrc(), lookup, depth + 1);
      case cir::CastKind::array_to_ptrdecay:
        return NullState::NonNull;
      default:
        return NullState::Unknown;
      }
    }
    if (auto ternary = mlir::dyn_cast<cir::TernaryOp>(def)) {
      std::optional<NullState> result;
      for (Region *region :
           {&ternary.getTrueRegion(), &ternary.getFalseRegion()}) {
        if (region->empty())
          continue;
        auto yield =
            mlir::dyn_cast<cir::YieldOp>(region->back().getTerminator());
        if (!yield || yield.getNumOperands() != 1)
          return NullState::Unknown;
        NullState state =
            regionState(ternary, region, yield.getOperand(0), lookup, depth);
        result = result ? joinStates(*result, state) : state;
      }
      return result.value_or(NullState::Unknown);
    }
    if (auto select = mlir::dyn_cast<cir::SelectOp>(def))
      return joinStates(valueState(select.getTrueValue(), lookup, depth + 1),
                        valueState(select.getFalseValue(), lookup, depth + 1));
    return NullState::Unknown;
  }

  NullState regionState(cir::TernaryOp ternary, Region *region, Value value,
                        const LatticeLookup &lookup, unsigned depth) const {
    ConditionFacts facts;
    conditionFacts(ternary.getCond(), region == &ternary.getTrueRegion(),
                   facts);
    Value stripped = stripCasts(value);
    for (auto &[fact, state] : facts.values)
      if (fact == stripped)
        return state;
    return valueState(value, lookup, depth + 1);
  }

  static bool isDiscardedDeref(Operation *op) {
    if (!op->use_empty())
      return false;
    if (auto load = mlir::dyn_cast<cir::LoadOp>(op))
      return load.getIsDeref();
    return mlir::isa<cir::GetMemberOp>(op);
  }

  static Value discardedDerefTarget(Operation *op) {
    if (auto load = mlir::dyn_cast<cir::LoadOp>(op))
      return load.getResult();
    return derefTarget(mlir::cast<cir::GetMemberOp>(op).getResult());
  }

  static Value derefTarget(Value addr) {
    addr = stripCasts(addr);
    Operation *def = addr.getDefiningOp();
    if (!def)
      return addr;
    if (mlir::isa<cir::AllocaOp, cir::GetGlobalOp>(def))
      return {};
    if (auto member = mlir::dyn_cast<cir::GetMemberOp>(def))
      return derefTarget(member.getAddr());
    if (auto stride = mlir::dyn_cast<cir::PtrStrideOp>(def))
      return derefTarget(stride.getBase());
    if (auto cast = mlir::dyn_cast<cir::CastOp>(def))
      if (cast.getKind() == cir::CastKind::array_to_ptrdecay)
        return derefTarget(cast.getSrc());
    return addr;
  }

  void addPointerFact(Value pointer, NullState state, ConditionFacts &out,
                      const NullLattice *lattice = nullptr) const {
    pointer = stripCasts(pointer);
    out.values.push_back({pointer, state});
    std::optional<AccessPath> path = pathOfValue(pointer);
    if (!path)
      return;
    out.paths.push_back({*path, state});
    if (!lattice || (state != NullState::NonNull && state != NullState::Null))
      return;
    if (auto found = lattice->guards.find(*path);
        found != lattice->guards.end()) {
      const PathFacts &implied = state == NullState::NonNull
                                     ? found->second.onTrue
                                     : found->second.onFalse;
      out.paths.append(implied.begin(), implied.end());
    }
  }

  static std::optional<bool> truthConstant(Value value) {
    value = stripCasts(value);
    while (auto cast = value.getDefiningOp<cir::CastOp>()) {
      if (cast.getKind() != cir::CastKind::bool_to_int &&
          cast.getKind() != cir::CastKind::int_to_bool &&
          cast.getKind() != cir::CastKind::integral)
        break;
      value = cast.getSrc();
    }
    if (auto constant = value.getDefiningOp<cir::ConstantOp>()) {
      if (auto b = mlir::dyn_cast<cir::BoolAttr>(constant.getValue()))
        return b.getValue();
      if (auto i = mlir::dyn_cast<cir::IntAttr>(constant.getValue()))
        return !i.getValue().isZero();
      if (auto ptr = mlir::dyn_cast<cir::ConstPtrAttr>(constant.getValue()))
        return !ptr.isNullValue();
    }
    return std::nullopt;
  }

  void conditionFacts(Value cond, bool truth, ConditionFacts &out,
                      const NullLattice *state = nullptr,
                      unsigned depth = 0) const {
    if (depth > 16)
      return;
    if (auto load = cond.getDefiningOp<cir::LoadOp>()) {
      if (!state)
        return;
      if (std::optional<AccessPath> path = pathOfAddress(load.getAddr()))
        if (auto found = state->guards.find(*path);
            found != state->guards.end()) {
          const PathFacts &facts =
              truth ? found->second.onTrue : found->second.onFalse;
          out.paths.append(facts.begin(), facts.end());
        }
      return;
    }
    if (auto cast = cond.getDefiningOp<cir::CastOp>()) {
      switch (cast.getKind()) {
      case cir::CastKind::ptr_to_bool:
        addPointerFact(cast.getSrc(),
                       truth ? NullState::NonNull : NullState::Null, out,
                       state);
        return;
      case cir::CastKind::int_to_bool:
      case cir::CastKind::bool_to_int:
      case cir::CastKind::integral:
        conditionFacts(cast.getSrc(), truth, out, state, depth + 1);
        return;
      default:
        return;
      }
    }
    if (auto call = cond.getDefiningOp<cir::CallOp>()) {
      std::optional<cir::SmartPtrOp> op = smartPtrOp(call);
      std::optional<AccessPath> object = smartObject(call);
      if (!op || !object)
        return;
      bool nonNull;
      if (*op == cir::SmartPtrOp::Test || *op == cir::SmartPtrOp::CompareNe)
        nonNull = truth;
      else if (*op == cir::SmartPtrOp::CompareEq)
        nonNull = !truth;
      else
        return;
      out.paths.push_back(
          {*object, nonNull ? NullState::NonNull : NullState::Null});
      return;
    }
    if (auto notOp = cond.getDefiningOp<cir::NotOp>()) {
      conditionFacts(notOp.getInput(), !truth, out, state, depth + 1);
      return;
    }
    if (auto cmp = cond.getDefiningOp<cir::CmpOp>()) {
      cir::CmpOpKind kind = cmp.getKind();
      if (kind != cir::CmpOpKind::eq && kind != cir::CmpOpKind::ne)
        return;
      Value other;
      if (isNullConstant(cmp.getRhs()))
        other = cmp.getLhs();
      else if (isNullConstant(cmp.getLhs()))
        other = cmp.getRhs();
      if (!other) {
        Value lhs = cmp.getLhs(), rhs = cmp.getRhs();
        std::optional<bool> constant = truthConstant(rhs);
        Value operand = lhs;
        if (!constant) {
          constant = truthConstant(lhs);
          operand = rhs;
        }
        if (!constant)
          return;
        bool equal = (kind == cir::CmpOpKind::eq) == truth;
        if (*constant) {
          if (equal)
            conditionFacts(operand, true, out, state, depth + 1);
        } else {
          conditionFacts(operand, !equal, out, state, depth + 1);
        }
        return;
      }
      if (!mlir::isa<cir::PointerType>(other.getType()))
        return;
      bool nonNull = (kind == cir::CmpOpKind::ne) == truth;
      addPointerFact(other, nonNull ? NullState::NonNull : NullState::Null, out,
                     state);
      return;
    }
    if (auto select = cond.getDefiningOp<cir::SelectOp>()) {
      std::optional<bool> trueConst = truthConstant(select.getTrueValue());
      std::optional<bool> falseConst = truthConstant(select.getFalseValue());
      if ((truth && falseConst == false) || (!truth && falseConst == true))
        conditionFacts(select.getCondition(), true, out, state, depth + 1);
      else if ((truth && trueConst == false) || (!truth && trueConst == true))
        conditionFacts(select.getCondition(), false, out, state, depth + 1);
      return;
    }
    if (auto ternary = cond.getDefiningOp<cir::TernaryOp>()) {
      auto yielded = [](Region &region) -> Value {
        if (region.empty())
          return {};
        auto yield =
            mlir::dyn_cast<cir::YieldOp>(region.back().getTerminator());
        return yield && yield.getNumOperands() == 1 ? yield.getOperand(0)
                                                    : Value();
      };
      Value onTrue = yielded(ternary.getTrueRegion());
      Value onFalse = yielded(ternary.getFalseRegion());
      if (!onTrue || !onFalse)
        return;
      std::optional<bool> trueConst = truthConstant(onTrue);
      std::optional<bool> falseConst = truthConstant(onFalse);
      if (truth && falseConst == false) {
        conditionFacts(ternary.getCond(), true, out, state, depth + 1);
        conditionFacts(onTrue, true, out, state, depth + 1);
      } else if (truth && trueConst == false) {
        conditionFacts(ternary.getCond(), false, out, state, depth + 1);
        conditionFacts(onFalse, true, out, state, depth + 1);
      } else if (!truth && trueConst == true) {
        conditionFacts(ternary.getCond(), false, out, state, depth + 1);
        conditionFacts(onFalse, false, out, state, depth + 1);
      } else if (!truth && falseConst == true) {
        conditionFacts(ternary.getCond(), true, out, state, depth + 1);
        conditionFacts(onTrue, false, out, state, depth + 1);
      }
    }
  }

  NullState useState(Value value, Operation *at,
                     const LatticeLookup &lookup) const {
    Value stripped = stripCasts(value);
    Operation *child = at;
    for (Operation *parent = at->getParentOp();
         parent && !mlir::isa<cir::FuncOp>(parent);
         child = parent, parent = parent->getParentOp()) {
      Value cond;
      if (auto ifOp = mlir::dyn_cast<cir::IfOp>(parent))
        cond = ifOp.getCondition();
      else if (auto ternary = mlir::dyn_cast<cir::TernaryOp>(parent))
        cond = ternary.getCond();
      if (!cond)
        continue;
      ConditionFacts facts;
      conditionFacts(cond, child->getParentRegion()->getRegionNumber() == 0,
                     facts);
      for (auto &[fact, state] : facts.values)
        if (fact == stripped)
          return state;
    }
    return valueState(value, lookup);
  }

  cir::FuncOp callee(cir::CallOp call) const {
    return call.resolveCalleeInTable(symbols);
  }

  bool isNoReturn(cir::CallOp call) const {
    if (call->hasAttr(cir::CIRDialect::getNoReturnAttrName()))
      return true;
    cir::FuncOp target = callee(call);
    return target && target->hasAttr(cir::CIRDialect::getNoReturnAttrName());
  }

  bool isNonnullParam(cir::FuncOp target, unsigned index) const {
    return target && index < target.getNumArguments() &&
           fromAttr(target.getArgAttr(
               index, cir::CIRDialect::getNullabilityAttrName())) ==
               NullState::NonNull;
  }

  static void killPath(NullLattice &state, const AccessPath &path) {
    for (auto it = state.facts.begin(); it != state.facts.end();)
      it = it->first.extends(path) ? state.facts.erase(it) : std::next(it);
    for (auto it = state.guards.begin(); it != state.guards.end();)
      it = it->first.extends(path) ||
                   it->second.mentions([&](const AccessPath &mentioned) {
                     return mentioned.extends(path);
                   })
               ? state.guards.erase(it)
               : std::next(it);
    for (auto it = state.contents.begin(); it != state.contents.end();)
      it = it->first.extends(path) ? state.contents.erase(it) : std::next(it);
  }

  static std::optional<cir::SmartPtrOp> smartPtrOp(Operation *op) {
    if (auto attr = op->getAttrOfType<cir::SmartPtrOpAttr>(
            cir::CIRDialect::getSmartPtrOpAttrName()))
      return attr.getValue();
    return std::nullopt;
  }

  std::optional<AccessPath> smartObject(cir::CallOp call,
                                        unsigned index = 0) const {
    auto args = call.getArgOperands();
    if (index >= args.size())
      return std::nullopt;
    return pathOfAddress(args[index]);
  }

  bool isSmartPtrSlot(const AccessPath &path) const {
    auto alloca = path.root.getDefiningOp<cir::AllocaOp>();
    return path.steps.empty() && alloca &&
           alloca->hasAttr(cir::CIRDialect::getSmartPtrSlotAttrName());
  }

  std::optional<NullState> pathState(const AccessPath &path,
                                     const NullLattice &state) const {
    if (auto found = state.facts.find(path); found != state.facts.end())
      return found->second;
    if (!path.steps.empty())
      return std::nullopt;
    if (auto alloca = path.root.getDefiningOp<cir::AllocaOp>())
      return declared(alloca);
    return std::nullopt;
  }

  void applySmartPtrOp(cir::CallOp call, cir::SmartPtrOp op,
                       const LatticeLookup &lookup, NullLattice &state) const {
    std::optional<AccessPath> object = smartObject(call);
    if (!object)
      return;
    auto set = [&](const AccessPath &path, std::optional<NullState> value) {
      killPath(state, path);
      if (value)
        state.facts[path] = *value;
    };
    switch (op) {
    case cir::SmartPtrOp::ConstructNull:
    case cir::SmartPtrOp::AssignNull:
    case cir::SmartPtrOp::Release:
      set(*object, NullState::Null);
      return;
    case cir::SmartPtrOp::ConstructFrom:
    case cir::SmartPtrOp::Reset: {
      auto args = call.getArgOperands();
      set(*object,
          args.size() > 1 ? valueState(args[1], lookup) : NullState::Null);
      return;
    }
    case cir::SmartPtrOp::Move:
    case cir::SmartPtrOp::Copy:
    case cir::SmartPtrOp::Swap: {
      std::optional<AccessPath> other = smartObject(call, 1);
      std::optional<NullState> objectState = pathState(*object, state);
      std::optional<NullState> otherState =
          other ? pathState(*other, state) : std::nullopt;
      set(*object, otherState);
      if (other && op == cir::SmartPtrOp::Move)
        set(*other, NullState::Null);
      else if (other && op == cir::SmartPtrOp::Swap)
        set(*other, objectState);
      return;
    }
    case cir::SmartPtrOp::Access:
    case cir::SmartPtrOp::Test:
    case cir::SmartPtrOp::CompareEq:
    case cir::SmartPtrOp::CompareNe:
      return;
    }
  }

  bool mayChangeBehindOurBack(const AccessPath &path) const {
    return path.throughPointer() || escaped.contains(path.root);
  }

  void dropThroughPointer(NullLattice &state) const {
    auto volatilePath = [&](const AccessPath &path) {
      return mayChangeBehindOurBack(path);
    };
    for (auto it = state.facts.begin(); it != state.facts.end();)
      it = volatilePath(it->first) ? state.facts.erase(it) : std::next(it);
    for (auto it = state.guards.begin(); it != state.guards.end();)
      it = volatilePath(it->first) || it->second.mentions(volatilePath)
               ? state.guards.erase(it)
               : std::next(it);
    for (auto it = state.contents.begin(); it != state.contents.end();)
      it = volatilePath(it->first) ? state.contents.erase(it) : std::next(it);
  }

  Value identity(Value value, const NullLattice &state) const {
    value = stripCasts(value);
    if (std::optional<AccessPath> path = pathOfValue(value))
      if (auto found = state.contents.find(*path);
          found != state.contents.end())
        return found->second;
    return value;
  }

  bool knownNonNull(const AccessPath &path, const NullLattice &state) const {
    if (auto found = state.facts.find(path); found != state.facts.end())
      return found->second == NullState::NonNull;
    if (!path.steps.empty())
      return false;
    if (auto alloca = path.root.getDefiningOp<cir::AllocaOp>())
      return declared(alloca) == NullState::NonNull;
    return false;
  }

  void applyFacts(const ConditionFacts &facts, const NullLattice &before,
                  NullLattice &state) const {
    for (auto &[path, fact] : facts.paths) {
      if (fact == NullState::Null && knownNonNull(path, before))
        continue;
      state.facts[path] = fact;
      auto held = before.contents.find(path);
      if (held == before.contents.end())
        continue;
      for (auto &[alias, value] : before.contents)
        if (value == held->second)
          state.facts[alias] = fact;
    }
    for (auto &[value, fact] : facts.values) {
      Value id = identity(value, before);
      for (auto &[path, held] : state.contents)
        if (held == id)
          state.facts[path] = fact;
    }
  }

  void narrowDereferenced(Value addr, const NullLattice &before,
                          NullLattice &state) const {
    if (Value target = derefTarget(addr)) {
      ConditionFacts facts;
      addPointerFact(target, NullState::NonNull, facts);
      applyFacts(facts, before, state);
    }
  }

  std::optional<std::pair<AccessPath, bool>> conditionSlot(Value cond) const {
    bool polarity = true;
    for (unsigned depth = 0; depth < 16; ++depth) {
      if (auto notOp = cond.getDefiningOp<cir::NotOp>()) {
        polarity = !polarity;
        cond = notOp.getInput();
        continue;
      }
      if (auto cast = cond.getDefiningOp<cir::CastOp>()) {
        cir::CastKind kind = cast.getKind();
        if (kind != cir::CastKind::int_to_bool &&
            kind != cir::CastKind::bool_to_int &&
            kind != cir::CastKind::integral)
          return std::nullopt;
        cond = cast.getSrc();
        continue;
      }
      if (auto load = cond.getDefiningOp<cir::LoadOp>())
        if (std::optional<AccessPath> path = pathOfAddress(load.getAddr()))
          return std::make_pair(*path, polarity);
      return std::nullopt;
    }
    return std::nullopt;
  }

  void recordConditionalStore(Value value, const AccessPath &path,
                              const LatticeLookup &lookup,
                              NullLattice &state) const {
    Value cond;
    NullState onTrue, onFalse;
    if (auto select = value.getDefiningOp<cir::SelectOp>()) {
      cond = select.getCondition();
      onTrue = valueState(select.getTrueValue(), lookup);
      onFalse = valueState(select.getFalseValue(), lookup);
    } else if (auto ternary = value.getDefiningOp<cir::TernaryOp>()) {
      auto armState = [&](Region &region) {
        if (region.empty())
          return NullState::Unknown;
        auto yield =
            mlir::dyn_cast<cir::YieldOp>(region.back().getTerminator());
        if (!yield || yield.getNumOperands() != 1)
          return NullState::Unknown;
        return regionState(ternary, &region, yield.getOperand(0), lookup, 0);
      };
      cond = ternary.getCond();
      onTrue = armState(ternary.getTrueRegion());
      onFalse = armState(ternary.getFalseRegion());
    }
    if (!cond || onTrue == onFalse)
      return;
    SmallVector<AccessPath> storedInArms;
    value.getDefiningOp()->walk([&](cir::StoreOp store) {
      if (std::optional<AccessPath> stored = pathOfAddress(store.getAddr()))
        storedInArms.push_back(*stored);
    });
    auto reassigned = [&](const AccessPath &mentioned) {
      return llvm::any_of(storedInArms, [&](const AccessPath &stored) {
        return mentioned.extends(stored);
      });
    };
    if (onTrue == NullState::Null || onFalse == NullState::Null) {
      ConditionFacts implied;
      conditionFacts(cond, onFalse == NullState::Null, implied, &state);
      llvm::erase_if(implied.paths,
                     [&](const auto &fact) { return reassigned(fact.first); });
      if (!implied.paths.empty()) {
        Guard &self = state.guards[path];
        self.onTrue.append(implied.paths.begin(), implied.paths.end());
      }
    }
    std::optional<std::pair<AccessPath, bool>> slot = conditionSlot(cond);
    if (!slot)
      return;
    if (reassigned(slot->first))
      return;
    Guard &guard = state.guards[slot->first];
    guard.onTrue.push_back({path, slot->second ? onTrue : onFalse});
    guard.onFalse.push_back({path, slot->second ? onFalse : onTrue});
  }

  const NullabilitySafetyCIROptions &options;
  const llvm::DenseSet<Value> &tracked;
  const llvm::DenseSet<Value> &escaped;
  const llvm::DenseSet<Operation *> &allReturnsNonnull;
  mlir::SymbolTableCollection &symbols;
};

class NullabilityAnalysis : public DenseForwardDataFlowAnalysis<NullLattice> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(NullabilityAnalysis)
  NullabilityAnalysis(DataFlowSolver &solver, const Semantics &semantics)
      : DenseForwardDataFlowAnalysis(solver), semantics(semantics) {}

  LogicalResult visitOperation(Operation *op, const NullLattice &before,
                               NullLattice *after) override {
    if (!before.reached)
      return success();
    NullLattice next = before;
    LatticeLookup lookup = [&](Operation *at) {
      return static_cast<const NullLattice *>(
          getLatticeFor(getProgramPointAfter(op), getProgramPointBefore(at)));
    };
    if (auto store = mlir::dyn_cast<cir::StoreOp>(op)) {
      NullState stored = semantics.useState(store.getValue(), op, lookup);
      if (std::optional<AccessPath> path =
              semantics.pathOfAddress(store.getAddr())) {
        Semantics::killPath(next, *path);
        if (semantics.mayChangeBehindOurBack(*path) &&
            mlir::isa<cir::PointerType>(store.getValue().getType()))
          semantics.dropThroughPointer(next);
        next.contents[*path] = semantics.identity(store.getValue(), before);
        if (semantics.isSmartPtrSlot(*path)) {
          if (stored != NullState::Unknown)
            next.facts[*path] = stored;
        } else if (mlir::isa<cir::PointerType>(store.getValue().getType())) {
          next.facts[*path] = stored;
          semantics.recordConditionalStore(store.getValue(), *path, lookup,
                                           next);
        } else {
          ConditionFacts whenTrue, whenFalse;
          semantics.conditionFacts(store.getValue(), true, whenTrue, &before);
          semantics.conditionFacts(store.getValue(), false, whenFalse, &before);
          if (!whenTrue.paths.empty() || !whenFalse.paths.empty())
            next.guards[*path] = Guard{whenTrue.paths, whenFalse.paths};
        }
      } else if (mlir::isa<cir::PointerType>(store.getValue().getType())) {
        semantics.dropThroughPointer(next);
      }
      semantics.narrowDereferenced(store.getAddr(), before, next);
    } else if (Semantics::isDiscardedDeref(op) && !mlir::isa<cir::LoadOp>(op)) {
      semantics.narrowDereferenced(mlir::cast<cir::GetMemberOp>(op).getResult(),
                                   before, next);
    } else if (auto load = mlir::dyn_cast<cir::LoadOp>(op)) {
      if (std::optional<AccessPath> path =
              semantics.pathOfAddress(load.getAddr())) {
        for (auto it = next.contents.begin(); it != next.contents.end();)
          it = it->second == load.getResult() ? next.contents.erase(it)
                                              : std::next(it);
        next.contents.try_emplace(*path, load.getResult());
      }
      semantics.narrowDereferenced(load.getAddr(), before, next);
      if (Semantics::isDiscardedDeref(op))
        if (std::optional<AccessPath> path =
                semantics.pathOfAddress(load.getAddr()))
          next.facts[*path] = NullState::NonNull;
    } else if (auto assume = mlir::dyn_cast<cir::AssumeOp>(op)) {
      ConditionFacts facts;
      semantics.conditionFacts(assume.getPredicate(), true, facts, &before);
      semantics.applyFacts(facts, before, next);
    }
    propagateIfChanged(after, after->join(next));
    return success();
  }

  void visitCallControlFlowTransfer(CallOpInterface call,
                                    CallControlFlowAction action,
                                    const NullLattice &before,
                                    NullLattice *after) override {
    if (!before.reached)
      return;
    NullLattice next = before;
    auto cirCall = mlir::dyn_cast<cir::CallOp>(call.getOperation());
    if (cirCall) {
      if (std::optional<cir::SmartPtrOp> op = Semantics::smartPtrOp(cirCall)) {
        LatticeLookup lookup = [&](Operation *at) {
          return static_cast<const NullLattice *>(getLatticeFor(
              getProgramPointAfter(cirCall), getProgramPointBefore(at)));
        };
        semantics.applySmartPtrOp(cirCall, *op, lookup, next);
        propagateIfChanged(after, after->join(next));
        return;
      }
    }
    semantics.dropThroughPointer(next);
    if (cirCall) {
      if (semantics.isNoReturn(cirCall))
        return;
      ConditionFacts facts;
      if (cirCall.isIndirect()) {
        semantics.addPointerFact(cirCall->getOperand(0), NullState::NonNull,
                                 facts);
      } else if (cir::FuncOp target = semantics.callee(cirCall)) {
        for (auto [index, arg] : llvm::enumerate(cirCall.getArgOperands()))
          if (semantics.isNonnullParam(target, index))
            semantics.addPointerFact(arg, NullState::NonNull, facts);
      }
      semantics.applyFacts(facts, before, next);
    }
    propagateIfChanged(after, after->join(next));
  }

  void visitRegionBranchControlFlowTransfer(RegionBranchOpInterface branch,
                                            std::optional<unsigned> regionFrom,
                                            std::optional<unsigned> regionTo,
                                            const NullLattice &before,
                                            NullLattice *after) override {
    if (!before.reached)
      return;
    NullLattice next = before;
    Value cond;
    bool truth = false;
    Operation *op = branch.getOperation();
    if (!regionFrom) {
      if (auto ifOp = mlir::dyn_cast<cir::IfOp>(op)) {
        cond = ifOp.getCondition();
        truth = regionTo && *regionTo == 0;
      } else if (auto ternary = mlir::dyn_cast<cir::TernaryOp>(op)) {
        cond = ternary.getCond();
        truth = regionTo && *regionTo == 0;
      }
    } else {
      Region &from = op->getRegion(*regionFrom);
      if (!from.empty())
        if (auto condition =
                mlir::dyn_cast<cir::ConditionOp>(from.back().getTerminator())) {
          cond = condition.getCondition();
          truth = regionTo.has_value();
        }
    }
    if (cond) {
      ConditionFacts facts;
      semantics.conditionFacts(cond, truth, facts, &before);
      semantics.applyFacts(facts, before, next);
    }
    propagateIfChanged(after, after->join(next));
  }

  void setToEntryState(NullLattice *lattice) override {
    propagateIfChanged(lattice, lattice->enter());
  }

private:
  const Semantics &semantics;
};

enum class SlotKind { Untracked, Local, Escaped };

SlotKind classifySlot(Value addr) {
  SlotKind kind = SlotKind::Local;
  for (OpOperand &use : addr.getUses()) {
    Operation *user = use.getOwner();
    if (mlir::isa<cir::LoadOp>(user))
      continue;
    if (auto store = mlir::dyn_cast<cir::StoreOp>(user)) {
      if (store.getAddr() != addr)
        kind = SlotKind::Escaped;
      continue;
    }
    if (auto member = mlir::dyn_cast<cir::GetMemberOp>(user)) {
      SlotKind inner = classifySlot(member.getResult());
      if (inner == SlotKind::Untracked)
        return SlotKind::Untracked;
      if (inner == SlotKind::Escaped)
        kind = SlotKind::Escaped;
      continue;
    }
    if (mlir::isa<cir::CallOp>(user)) {
      if (!user->hasAttr(cir::CIRDialect::getSmartPtrOpAttrName()))
        kind = SlotKind::Escaped;
      continue;
    }
    return SlotKind::Untracked;
  }
  return kind;
}

std::optional<unsigned> firstLine(Location loc) {
  std::optional<unsigned> line;
  loc->walk([&](Location inner) {
    if (auto file = mlir::dyn_cast<FileLineColLoc>(inner)) {
      line = file.getLine();
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });
  return line;
}

bool isInitializingStore(cir::StoreOp store, cir::AllocaOp slot) {
  if (!slot.getInit())
    return false;
  for (Operation *user : slot.getAddr().getUsers())
    if (auto first = mlir::dyn_cast<cir::StoreOp>(user))
      if (first.getAddr() == slot.getAddr() && first->isBeforeInBlock(store))
        return false;
  std::optional<unsigned> storeLine = firstLine(store.getLoc());
  return storeLine && storeLine == firstLine(slot.getLoc());
}

bool isSubscriptBase(Value base) {
  auto load = Semantics::stripCasts(base).getDefiningOp<cir::LoadOp>();
  return load && load.getIsDeref();
}

std::string describe(Value pointer) {
  pointer = Semantics::stripCasts(pointer);
  if (auto load = pointer.getDefiningOp<cir::LoadOp>()) {
    if (auto alloca = load.getAddr().getDefiningOp<cir::AllocaOp>())
      return ("'" + alloca.getName() + "'").str();
    if (auto member = load.getAddr().getDefiningOp<cir::GetMemberOp>())
      return ("'" + member.getName() + "'").str();
  }
  if (auto call = pointer.getDefiningOp<cir::CallOp>())
    if (auto callee = call.getCalleeAttr())
      return ("returned by '" + callee.getValue() + "'").str();
  return "value";
}

void reportFunction(
    cir::FuncOp func, DataFlowSolver &solver, const Semantics &semantics,
    llvm::function_ref<void(const NullabilityFinding &)> report) {
  LatticeLookup lookup = [&](Operation *at) -> const NullLattice * {
    const NullLattice *state =
        solver.lookupState<NullLattice>(solver.getProgramPointBefore(at));
    return state && state->reached ? state : nullptr;
  };
  bool nonnullResult =
      func.getNumResults() == 1 &&
      semantics.fromAttr(func.getResultAttr(
          0, cir::CIRDialect::getNullabilityAttrName())) == NullState::NonNull;

  auto checkDeref = [&](Operation *op, Value addr) {
    Value target = Semantics::derefTarget(addr);
    if (target && mayBeNull(semantics.useState(target, op, lookup)))
      report({NullabilityFindingKind::Dereference, op->getLoc(),
              describe(target)});
  };

  func.walk([&](Operation *op) {
    if (!lookup(op))
      return;
    if (Semantics::isDiscardedDeref(op)) {
      Value target = Semantics::discardedDerefTarget(op);
      if (target && mayBeNull(semantics.useState(target, op, lookup))) {
        Operation *def = target.getDefiningOp();
        report({NullabilityFindingKind::Dereference,
                def ? def->getLoc() : op->getLoc(), describe(target)});
      }
    }
    if (auto load = mlir::dyn_cast<cir::LoadOp>(op)) {
      checkDeref(op, load.getAddr());
      return;
    }
    if (auto store = mlir::dyn_cast<cir::StoreOp>(op)) {
      checkDeref(op, store.getAddr());
      auto slot = store.getAddr().getDefiningOp<cir::AllocaOp>();
      if (!slot || mlir::isa<BlockArgument>(store.getValue()))
        return;
      NullState stored = semantics.useState(store.getValue(), op, lookup);
      if (!mayBeNull(stored))
        return;
      if (slot.getName() == "__retval") {
        if (nonnullResult)
          report({NullabilityFindingKind::Return, op->getLoc(), ""});
        return;
      }
      if (semantics.declared(slot) == NullState::NonNull &&
          !(Semantics::isNullConstant(store.getValue()) &&
            isInitializingStore(store, slot)))
        report({NullabilityFindingKind::Assignment, op->getLoc(),
                ("'" + slot.getName() + "'").str()});
      return;
    }
    if (auto stride = mlir::dyn_cast<cir::PtrStrideOp>(op)) {
      bool addressOnly = llvm::any_of(stride->getUsers(), [&](Operation *u) {
        auto store = mlir::dyn_cast<cir::StoreOp>(u);
        return mlir::isa<cir::LoadOp, cir::GetMemberOp>(u) ||
               (store && store.getAddr() == stride.getResult());
      });
      if (!addressOnly &&
          mayBeNull(semantics.useState(stride.getBase(), op, lookup)))
        report({isSubscriptBase(stride.getBase()) && stride->use_empty()
                    ? NullabilityFindingKind::Dereference
                    : NullabilityFindingKind::Arithmetic,
                op->getLoc(), describe(stride.getBase())});
      return;
    }
    if (auto call = mlir::dyn_cast<cir::CallOp>(op)) {
      if (call.isIndirect()) {
        Value callee = call->getOperand(0);
        if (mayBeNull(semantics.useState(callee, op, lookup)))
          report(
              {NullabilityFindingKind::Call, op->getLoc(), describe(callee)});
        return;
      }
      cir::FuncOp callee = semantics.callee(call);
      if (!callee)
        return;
      for (auto [index, arg] : llvm::enumerate(call.getArgOperands())) {
        if (index >= callee.getNumArguments())
          break;
        if (semantics.fromAttr(callee.getArgAttr(
                index, cir::CIRDialect::getNullabilityAttrName())) !=
            NullState::NonNull)
          continue;
        if (!mayBeNull(semantics.useState(arg, op, lookup)))
          continue;
        if (callee.getArgAttr(index, cir::CIRDialect::getObjectArgAttrName()))
          report({NullabilityFindingKind::Dereference, op->getLoc(),
                  describe(arg)});
        else
          report({NullabilityFindingKind::Argument, op->getLoc(),
                  ("'" + callee.getSymName() + "'").str(),
                  static_cast<unsigned>(index)});
      }
    }
  });
}

bool returnsNonnullOnEveryPath(cir::FuncOp func, DataFlowSolver &solver,
                               const Semantics &semantics) {
  if (func.getNumResults() != 1 ||
      !mlir::isa<cir::PointerType>(func.getResultTypes()[0]) ||
      Semantics::isExplicit(
          func.getResultAttr(0, cir::CIRDialect::getNullabilityAttrName())))
    return false;
  LatticeLookup lookup = [&](Operation *at) -> const NullLattice * {
    const NullLattice *state =
        solver.lookupState<NullLattice>(solver.getProgramPointBefore(at));
    return state && state->reached ? state : nullptr;
  };
  bool sawReturn = false;
  WalkResult result = func.walk([&](cir::ReturnOp ret) {
    if (!lookup(ret) || ret.getNumOperands() != 1)
      return WalkResult::advance();
    sawReturn = true;
    return semantics.useState(ret.getOperand(0), ret, lookup) ==
                   NullState::NonNull
               ? WalkResult::advance()
               : WalkResult::interrupt();
  });
  return sawReturn && !result.wasInterrupted();
}

std::string declaredName(Attribute attr) {
  auto kind = mlir::dyn_cast_if_present<cir::NullabilityAttr>(attr);
  if (!kind)
    return "";
  switch (kind.getValue()) {
  case cir::Nullability::NonNull:
    return "nonnull";
  case cir::Nullability::Nullable:
    return "nullable";
  case cir::Nullability::Unspecified:
    return "unspecified";
  }
  return "";
}

std::string declaredName(cir::FuncOp func, std::optional<unsigned> arg) {
  Attribute attr =
      arg ? func.getArgAttr(*arg, cir::CIRDialect::getNullabilityAttrName())
          : func.getResultAttr(0, cir::CIRDialect::getNullabilityAttrName());
  std::string name = declaredName(attr);
  if (!name.empty() && name != "unspecified")
    return name;
  StringRef abiNonNull = LLVM::LLVMDialect::getNonNullAttrName();
  if (arg ? func.getArgAttr(*arg, abiNonNull)
          : func.getResultAttr(0, abiNonNull))
    return "abi_nonnull";
  return name;
}

InferredNullability inferFact(const NullabilityFact &fact) {
  if (fact.declared == "nonnull" || fact.declared == "abi_nonnull")
    return InferredNullability::NonNull;
  if (fact.declared == "nullable")
    return InferredNullability::Nullable;
  if (fact.assignedNull || fact.callersPassNull || fact.nullChecked)
    return InferredNullability::Nullable;
  if (fact.kind == NullabilityFactKind::Return)
    return fact.allPathsNonNull ? InferredNullability::NonNull
                                : InferredNullability::Unknown;
  if (fact.derefUnchecked || fact.forwardedToNonNull || fact.assertedNonNull)
    return InferredNullability::NonNull;
  return InferredNullability::Unknown;
}

struct FactCollector {
  explicit FactCollector(llvm::function_ref<bool(Location)> include)
      : include(include) {}

  llvm::function_ref<bool(Location)> include;
  std::vector<NullabilityFact> facts;
  llvm::DenseMap<std::pair<Operation *, unsigned>, size_t> params;
  std::map<std::pair<std::string, std::string>, size_t> fields;
  llvm::DenseSet<std::pair<Operation *, unsigned>> passedNull;

  const NullabilityFact *param(Operation *func, unsigned index) const {
    auto found = params.find({func, index});
    return found == params.end() ? nullptr : &facts[found->second];
  }
};

std::string recordName(Type type) {
  auto pointer = mlir::dyn_cast<cir::PointerType>(type);
  if (!pointer)
    return "";
  StringAttr name;
  if (auto record = mlir::dyn_cast<cir::StructType>(pointer.getPointee()))
    name = record.getName();
  else if (auto record = mlir::dyn_cast<cir::UnionType>(pointer.getPointee()))
    name = record.getName();
  return name ? name.getValue().str() : "<anonymous>";
}

bool guardsNoReturn(Value cond, const Semantics &semantics) {
  SmallVector<std::pair<Value, unsigned>> worklist{{cond, 0}};
  auto noReturnIn = [&](Region &region) {
    return region
        .walk([&](cir::CallOp call) {
          return semantics.isNoReturn(call) ? WalkResult::interrupt()
                                            : WalkResult::advance();
        })
        .wasInterrupted();
  };
  while (!worklist.empty()) {
    auto [value, depth] = worklist.pop_back_val();
    if (depth > 6)
      continue;
    for (Operation *user : value.getUsers()) {
      SmallVector<Region *, 2> arms;
      if (auto ifOp = mlir::dyn_cast<cir::IfOp>(user))
        arms = {&ifOp.getThenRegion(), &ifOp.getElseRegion()};
      else if (auto ternary = mlir::dyn_cast<cir::TernaryOp>(user))
        arms = {&ternary.getTrueRegion(), &ternary.getFalseRegion()};
      if (!arms.empty()) {
        if (noReturnIn(*arms[0]) != noReturnIn(*arms[1]))
          return true;
        continue;
      }
      if (mlir::isa<cir::CallOp, cir::StoreOp>(user))
        continue;
      for (Value result : user->getResults())
        worklist.push_back({result, depth + 1});
    }
  }
  return false;
}

enum class PathResult { FallThrough, Deref, Fail };

class MustDeref {
public:
  MustDeref(Value slot, const Semantics &semantics, const FactCollector &out)
      : slot(slot), semantics(semantics), out(out) {}

  PathResult region(Region &region) {
    if (region.empty())
      return PathResult::FallThrough;
    if (!region.hasOneBlock())
      return leaves(region) ? PathResult::Fail : PathResult::FallThrough;
    for (Operation &op : region.front())
      if (PathResult result = visit(&op); result != PathResult::FallThrough)
        return result;
    return PathResult::FallThrough;
  }

private:
  static PathResult both(PathResult a, PathResult b) {
    if (a == PathResult::Fail || b == PathResult::Fail)
      return PathResult::Fail;
    if (a == PathResult::Deref && b == PathResult::Deref)
      return PathResult::Deref;
    return PathResult::FallThrough;
  }

  bool fromSlot(Value value) const {
    if (!value)
      return false;
    auto load = Semantics::stripCasts(value).getDefiningOp<cir::LoadOp>();
    return load && load.getAddr() == slot;
  }

  bool storesSlot(Operation *op) const {
    auto store = mlir::dyn_cast<cir::StoreOp>(op);
    return store && store.getAddr() == slot &&
           !mlir::isa<BlockArgument>(store.getValue());
  }

  bool exits(Operation *op) const {
    return storesSlot(op) ||
           mlir::isa<cir::ReturnOp, cir::GotoOp, cir::ThrowOp, cir::ResumeOp,
                     cir::TrapOp, cir::CallOp, cir::TryCallOp>(op);
  }

  bool leaves(Region &region) const {
    return region
        .walk([&](Operation *op) {
          return exits(op) ? WalkResult::interrupt() : WalkResult::advance();
        })
        .wasInterrupted();
  }

  bool derefs(Operation *op) const {
    if (auto load = mlir::dyn_cast<cir::LoadOp>(op))
      return fromSlot(Semantics::derefTarget(load.getAddr()));
    if (auto store = mlir::dyn_cast<cir::StoreOp>(op))
      return fromSlot(Semantics::derefTarget(store.getAddr()));
    if (auto member = mlir::dyn_cast<cir::GetMemberOp>(op))
      return fromSlot(member.getAddr());
    auto call = mlir::dyn_cast<cir::CallOp>(op);
    if (!call || call.isIndirect() || Semantics::smartPtrOp(call))
      return false;
    cir::FuncOp callee = semantics.callee(call);
    if (!callee)
      return false;
    for (auto [index, arg] : llvm::enumerate(call.getArgOperands())) {
      if (index >= callee.getNumArguments() || !fromSlot(arg))
        continue;
      if (callee.getArgAttr(index, cir::CIRDialect::getObjectArgAttrName()))
        return true;
      if (const NullabilityFact *fact = out.param(callee, index))
        if (fact->mustDeref)
          return true;
    }
    return false;
  }

  PathResult visit(Operation *op) {
    if (derefs(op))
      return PathResult::Deref;
    if (exits(op) || mlir::isa<cir::BreakOp, cir::ContinueOp>(op))
      return PathResult::Fail;
    if (mlir::isa<cir::UnreachableOp>(op))
      return PathResult::Deref;
    if (auto ifOp = mlir::dyn_cast<cir::IfOp>(op))
      return both(region(ifOp.getThenRegion()), region(ifOp.getElseRegion()));
    if (auto ternary = mlir::dyn_cast<cir::TernaryOp>(op))
      return both(region(ternary.getTrueRegion()),
                  region(ternary.getFalseRegion()));
    if (auto scope = mlir::dyn_cast<cir::ScopeOp>(op))
      return region(scope.getScopeRegion());
    if (auto cleanup = mlir::dyn_cast<cir::CleanupScopeOp>(op))
      return region(cleanup.getBodyRegion());
    if (auto loop = mlir::dyn_cast<cir::LoopOpInterface>(op)) {
      Region &entry = loop.getEntry();
      if (PathResult result = region(entry); result != PathResult::FallThrough)
        return result;
      for (Region &other : op->getRegions())
        if (&other != &entry && leaves(other))
          return PathResult::Fail;
      return PathResult::FallThrough;
    }
    for (Region &nested : op->getRegions())
      if (leaves(nested))
        return PathResult::Fail;
    return PathResult::FallThrough;
  }

  Value slot;
  const Semantics &semantics;
  const FactCollector &out;
};

void collectFunctionFacts(cir::FuncOp func, DataFlowSolver &solver,
                          const Semantics &semantics, FactCollector &out) {
  if (func.getBody().empty() || !out.include(func.getLoc()))
    return;
  LatticeLookup lookup = [&](Operation *at) -> const NullLattice * {
    const NullLattice *state =
        solver.lookupState<NullLattice>(solver.getProgramPointBefore(at));
    return state && state->reached ? state : nullptr;
  };
  std::string symbol = func.getSymName().str();
  llvm::DenseMap<Value, size_t> slots;
  Block &entry = func.getBody().front();
  for (BlockArgument arg : entry.getArguments()) {
    unsigned index = arg.getArgNumber();
    if (!mlir::isa<cir::PointerType>(arg.getType()) ||
        func.getArgAttr(index, cir::CIRDialect::getObjectArgAttrName()))
      continue;
    for (Operation *user : arg.getUsers()) {
      auto store = mlir::dyn_cast<cir::StoreOp>(user);
      auto slot = store ? store.getAddr().getDefiningOp<cir::AllocaOp>()
                        : cir::AllocaOp();
      if (!slot || store.getValue() != arg)
        continue;
      if (slot.getName() == "this")
        break;
      NullabilityFact fact(NullabilityFactKind::Param, slot.getLoc());
      fact.function = symbol;
      fact.name = slot.getName().str();
      fact.index = index;
      fact.declared = declaredName(func, index);
      slots[slot.getAddr()] = out.facts.size();
      out.params[{func, index}] = out.facts.size();
      out.facts.push_back(fact);
      break;
    }
  }
  std::optional<size_t> returnFact;
  if (func.getNumResults() == 1 &&
      mlir::isa<cir::PointerType>(func.getResultTypes()[0])) {
    NullabilityFact fact(NullabilityFactKind::Return, func.getLoc());
    fact.function = symbol;
    fact.declared = declaredName(func, std::nullopt);
    returnFact = out.facts.size();
    out.facts.push_back(fact);
  }

  llvm::DenseMap<Value, size_t> copies;
  func.walk([&](cir::StoreOp store) {
    auto local = store.getAddr().getDefiningOp<cir::AllocaOp>();
    auto load =
        Semantics::stripCasts(store.getValue()).getDefiningOp<cir::LoadOp>();
    if (!local || !load || !local.getInit() ||
        slots.contains(local.getAddr()) ||
        store->getBlock() != local->getBlock())
      return;
    for (Operation *user : local.getAddr().getUsers())
      if (auto other = mlir::dyn_cast<cir::StoreOp>(user))
        if (other.getAddr() == local.getAddr() &&
            other->getBlock() == store->getBlock() &&
            other->isBeforeInBlock(store))
          return;
    if (auto param = slots.find(load.getAddr()); param != slots.end())
      copies[local.getAddr()] = param->second;
  });

  auto fieldFact = [&](cir::GetMemberOp member, Location loc) -> size_t {
    std::pair<std::string, std::string> key{
        recordName(member.getAddr().getType()), member.getName().str()};
    auto [it, inserted] = out.fields.try_emplace(key, out.facts.size());
    if (inserted) {
      NullabilityFact fact(NullabilityFactKind::Field, loc);
      fact.record = key.first;
      fact.name = key.second;
      out.facts.push_back(fact);
    }
    return it->second;
  };
  auto targets = [&](Value pointer, Location loc) {
    SmallVector<size_t, 2> found;
    pointer = Semantics::stripCasts(pointer);
    if (std::optional<AccessPath> path = semantics.pathOfValue(pointer))
      if (path->steps.empty())
        if (auto slot = slots.find(path->root); slot != slots.end())
          found.push_back(slot->second);
        else if (auto copy = copies.find(path->root); copy != copies.end())
          found.push_back(copy->second);
    if (auto load = pointer.getDefiningOp<cir::LoadOp>())
      if (auto member = load.getAddr().getDefiningOp<cir::GetMemberOp>())
        if (mlir::isa<cir::PointerType>(load.getType()))
          found.push_back(fieldFact(member, loc));
    return found;
  };
  auto mark = [&](Value pointer, Location loc, bool NullabilityFact::*flag) {
    for (size_t index : targets(pointer, loc))
      out.facts[index].*flag = true;
  };
  auto derefAt = [&](Operation *op, Value pointer) {
    if (pointer &&
        semantics.useState(pointer, op, lookup) != NullState::NonNull)
      mark(pointer, op->getLoc(), &NullabilityFact::derefUnchecked);
  };
  auto checked = [&](Operation *op, Value pointer) {
    mark(pointer, op->getLoc(),
         guardsNoReturn(op->getResult(0), semantics)
             ? &NullabilityFact::assertedNonNull
             : &NullabilityFact::nullChecked);
  };

  func.walk([&](Operation *op) {
    if (!lookup(op))
      return;
    if (auto cmp = mlir::dyn_cast<cir::CmpOp>(op)) {
      if (cmp.getKind() != cir::CmpOpKind::eq &&
          cmp.getKind() != cir::CmpOpKind::ne)
        return;
      if (Semantics::isNullConstant(cmp.getRhs()) &&
          mlir::isa<cir::PointerType>(cmp.getLhs().getType()))
        checked(op, cmp.getLhs());
      else if (Semantics::isNullConstant(cmp.getLhs()) &&
               mlir::isa<cir::PointerType>(cmp.getRhs().getType()))
        checked(op, cmp.getRhs());
      return;
    }
    if (auto cast = mlir::dyn_cast<cir::CastOp>(op)) {
      if (cast.getKind() == cir::CastKind::ptr_to_bool)
        checked(op, cast.getSrc());
      return;
    }
    if (auto load = mlir::dyn_cast<cir::LoadOp>(op)) {
      derefAt(op, Semantics::derefTarget(load.getAddr()));
      return;
    }
    if (Semantics::isDiscardedDeref(op)) {
      derefAt(op, Semantics::discardedDerefTarget(op));
      return;
    }
    if (auto store = mlir::dyn_cast<cir::StoreOp>(op)) {
      derefAt(op, Semantics::derefTarget(store.getAddr()));
      Value value = store.getValue();
      if (!mlir::isa<cir::PointerType>(value.getType()) ||
          mlir::isa<BlockArgument>(value) ||
          !mayBeNull(semantics.useState(value, op, lookup)))
        return;
      if (auto slot = slots.find(store.getAddr()); slot != slots.end())
        out.facts[slot->second].assignedNull = true;
      if (auto alloca = store.getAddr().getDefiningOp<cir::AllocaOp>())
        if (returnFact && alloca.getName() == "__retval")
          out.facts[*returnFact].assignedNull = true;
      if (auto member = store.getAddr().getDefiningOp<cir::GetMemberOp>())
        out.facts[fieldFact(member, op->getLoc())].assignedNull = true;
      return;
    }
    auto call = mlir::dyn_cast<cir::CallOp>(op);
    if (!call || Semantics::smartPtrOp(call))
      return;
    if (call.isIndirect()) {
      derefAt(op, call->getOperand(0));
      return;
    }
    cir::FuncOp callee = semantics.callee(call);
    if (!callee)
      return;
    for (auto [index, arg] : llvm::enumerate(call.getArgOperands())) {
      if (index >= callee.getNumArguments() ||
          !mlir::isa<cir::PointerType>(arg.getType()))
        continue;
      if (callee.getArgAttr(index, cir::CIRDialect::getObjectArgAttrName())) {
        derefAt(op, arg);
        continue;
      }
      NullState state = semantics.useState(arg, op, lookup);
      if (mayBeNull(state))
        out.passedNull.insert({callee, static_cast<unsigned>(index)});
      const NullabilityFact *target = out.param(callee, index);
      bool nonnullParam =
          semantics.isNonnullParam(callee, index) ||
          (target && inferFact(*target) == InferredNullability::NonNull);
      if (nonnullParam && state != NullState::NonNull)
        mark(arg, op->getLoc(), &NullabilityFact::forwardedToNonNull);
    }
  });

  for (auto [slot, index] : slots) {
    NullabilityFact &fact = out.facts[index];
    fact.addressTaken =
        semantics.escaped.contains(slot) || !semantics.tracked.contains(slot);
    for (Operation *user : slot.getUsers()) {
      auto store = mlir::dyn_cast<cir::StoreOp>(user);
      if (store && store.getAddr() == slot &&
          !mlir::isa<BlockArgument>(store.getValue()))
        fact.reassigned = true;
      if (auto load = mlir::dyn_cast<cir::LoadOp>(user))
        for (Operation *loadUser : load->getUsers())
          if (auto stride = mlir::dyn_cast<cir::PtrStrideOp>(loadUser))
            if (Semantics::stripCasts(stride.getBase()) == load.getResult())
              fact.arithmetic = true;
    }
    fact.mustDeref = MustDeref(slot, semantics, out).region(func.getBody()) ==
                     PathResult::Deref;
  }

  if (returnFact) {
    bool sawReturn = false;
    WalkResult result = func.walk([&](cir::ReturnOp ret) {
      if (!lookup(ret) || ret.getNumOperands() != 1)
        return WalkResult::advance();
      sawReturn = true;
      return semantics.useState(ret.getOperand(0), ret, lookup) ==
                     NullState::NonNull
                 ? WalkResult::advance()
                 : WalkResult::interrupt();
    });
    out.facts[*returnFact].allPathsNonNull =
        sawReturn && !result.wasInterrupted();
  }
}

void analyzeFunction(
    cir::FuncOp func, bool infer, const NullabilitySafetyCIROptions &options,
    llvm::DenseSet<Operation *> &allReturnsNonnull,
    mlir::SymbolTableCollection &symbols,
    llvm::function_ref<void(const NullabilityFinding &)> report,
    FactCollector *facts = nullptr) {
  llvm::DenseSet<Value> tracked, escaped;
  func.walk([&](cir::AllocaOp alloca) {
    SlotKind kind = classifySlot(alloca.getAddr());
    if (kind != SlotKind::Untracked)
      tracked.insert(alloca.getAddr());
    if (kind == SlotKind::Escaped)
      escaped.insert(alloca.getAddr());
  });
  Semantics semantics(options, tracked, escaped, allReturnsNonnull, symbols);
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<DeadCodeAnalysis>();
  solver.load<SparseConstantPropagation>();
  solver.load<NullabilityAnalysis>(semantics);
  if (failed(solver.initializeAndRun(func)))
    return;
  if (infer && returnsNonnullOnEveryPath(func, solver, semantics))
    allReturnsNonnull.insert(func);
  if (facts)
    collectFunctionFacts(func, solver, semantics, *facts);
  else if (func->hasAttr(cir::CIRDialect::getNullabilitySafetyAttrName()))
    reportFunction(func, solver, semantics, report);
}

void analyzeModule(mlir::ModuleOp module,
                   const NullabilitySafetyCIROptions &options,
                   llvm::function_ref<void(const NullabilityFinding &)> report,
                   FactCollector *facts) {
  mlir::CallGraph callGraph(module);
  llvm::DenseSet<Operation *> allReturnsNonnull;
  mlir::SymbolTableCollection symbols;
  for (auto scc = llvm::scc_begin<const mlir::CallGraph *>(&callGraph);
       !scc.isAtEnd(); ++scc) {
    bool recursive = scc.hasCycle();
    for (mlir::CallGraphNode *node : *scc) {
      if (node->isExternal())
        continue;
      auto func =
          mlir::dyn_cast<cir::FuncOp>(node->getCallableRegion()->getParentOp());
      if (!func || func.isDeclaration())
        continue;
      analyzeFunction(func, !recursive, options, allReturnsNonnull, symbols, report,
                      facts);
    }
  }
}

} // namespace

void runNullabilitySafety(
    mlir::ModuleOp module, const NullabilitySafetyCIROptions &options,
    llvm::function_ref<void(const NullabilityFinding &)> report) {
  analyzeModule(module, options, report, nullptr);
}

void collectNullabilityFacts(
    mlir::ModuleOp module, const NullabilitySafetyCIROptions &options,
    llvm::function_ref<bool(mlir::Location)> include,
    llvm::function_ref<void(const NullabilityFact &)> emit) {
  FactCollector collector(include);
  analyzeModule(module, options, [](const NullabilityFinding &) {}, &collector);
  for (auto [func, index] : collector.passedNull)
    if (auto found = collector.params.find({func, index});
        found != collector.params.end())
      collector.facts[found->second].callersPassNull = true;
  for (NullabilityFact &fact : collector.facts) {
    fact.inferred = inferFact(fact);
    emit(fact);
  }
}

} // namespace cir
