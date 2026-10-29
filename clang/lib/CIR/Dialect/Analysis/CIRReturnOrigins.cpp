//===- CIRReturnOrigins.cpp - Which arguments a returned pointer comes from
//===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Walks back from each returned value. A load from a local slot (a spilled
// parameter, the return value slot, a local pointer or view) stands for
// everything put into it, which over-approximates what that load can see;
// that is the safe direction here, since an extra origin only adds a
// diagnostic.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/Analysis/CIRReturnOrigins.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"

using namespace cir;

namespace {

// The alloca an address is, through casts.
cir::AllocaOp slotOf(mlir::Value addr) {
  while (auto cast = addr.getDefiningOp<cir::CastOp>())
    addr = cast.getSrc();
  return addr.getDefiningOp<cir::AllocaOp>();
}

class Walker {
public:
  Walker(cir::FuncOp func, mlir::SymbolTableCollection &symbols,
         llvm::function_ref<bool(cir::FuncOp, unsigned)> carries)
      : entry(func.getBody().front()), symbols(symbols), carries(carries) {}

  llvm::SetVector<unsigned> args;
  /// Set when the value is the address of, or into, a global.
  bool global = false;

  /// What the slot an address names holds; nothing for any other address.
  void contents(mlir::Value addr) {
    if (cir::AllocaOp s = slotOf(addr))
      slot(s);
  }

  void value(mlir::Value v) {
    if (!seen.insert(v).second)
      return;
    if (auto arg = mlir::dyn_cast<mlir::BlockArgument>(v)) {
      if (arg.getOwner() == &entry)
        args.insert(arg.getArgNumber());
      return;
    }
    mlir::Operation *op = v.getDefiningOp();
    if (mlir::isa<cir::GetGlobalOp>(op)) {
      global = true;
      return;
    }
    if (auto load = mlir::dyn_cast<cir::LoadOp>(op)) {
      if (cir::AllocaOp s = slotOf(load.getAddr()))
        slot(s);
      return;
    }
    if (mlir::isa<cir::GetMemberOp, cir::GetElementOp, cir::PtrStrideOp,
                  cir::BaseClassAddrOp, cir::DerivedClassAddrOp>(op))
      return value(op->getOperand(0));
    if (auto cast = mlir::dyn_cast<cir::CastOp>(op)) {
      if (mlir::isa<cir::PointerType>(cast.getSrc().getType()))
        value(cast.getSrc());
      return;
    }
    if (auto select = mlir::dyn_cast<cir::SelectOp>(op)) {
      value(select.getTrueValue());
      value(select.getFalseValue());
      return;
    }
    if (auto ternary = mlir::dyn_cast<cir::TernaryOp>(op)) {
      unsigned i = mlir::cast<mlir::OpResult>(v).getResultNumber();
      ternary->walk([&](cir::YieldOp yield) {
        if (yield->getParentOp() == ternary && i < yield.getNumOperands())
          value(yield.getOperand(i));
      });
    }
  }

private:
  // What a local slot can hold: values stored into it, an object a
  // constructor builds in it, or what another slot copied into it. A copy
  // from anywhere else copies a pointer kept in memory, whose origin is not
  // traced.
  void slot(cir::AllocaOp alloca) {
    if (!seenSlots.insert(alloca).second)
      return;
    llvm::SmallVector<mlir::Value> addrs{alloca.getResult()};
    for (unsigned i = 0; i < addrs.size(); ++i)
      for (mlir::Operation *user : addrs[i].getUsers()) {
        if (auto cast = mlir::dyn_cast<cir::CastOp>(user)) {
          if (cast.getKind() == cir::CastKind::bitcast)
            addrs.push_back(cast.getResult());
        } else if (auto store = mlir::dyn_cast<cir::StoreOp>(user)) {
          if (store.getAddr() == addrs[i])
            value(store.getValue());
        } else if (auto copy = mlir::dyn_cast<cir::CopyOp>(user)) {
          if (copy.getDst() == addrs[i])
            if (cir::AllocaOp src = slotOf(copy.getSrc()))
              slot(src);
        } else if (auto call = mlir::dyn_cast<cir::CallOp>(user)) {
          constructed(call, addrs[i]);
        }
      }
  }

  void constructed(cir::CallOp call, mlir::Value object) {
    auto args = call.getArgOperands();
    mlir::FlatSymbolRefAttr name = call.getCalleeAttr();
    if (args.empty() || args[0] != object || !name)
      return;
    auto ctor = symbols.lookupNearestSymbolFrom<cir::FuncOp>(call, name);
    if (!ctor)
      return;
    for (unsigned i = 1; i < args.size(); ++i)
      if (mlir::isa<cir::PointerType>(args[i].getType()) && carries(ctor, i))
        value(args[i]);
  }

  mlir::Block &entry;
  mlir::SymbolTableCollection &symbols;
  llvm::function_ref<bool(cir::FuncOp, unsigned)> carries;
  llvm::DenseSet<mlir::Value> seen;
  llvm::DenseSet<mlir::Operation *> seenSlots;
};

} // namespace

void cir::collectReturnOrigins(
    mlir::ModuleOp module,
    llvm::function_ref<bool(cir::FuncOp, unsigned)> carries,
    llvm::function_ref<void(const ReturnOriginFact &)> emit) {
  mlir::SymbolTableCollection symbols;
  module.walk([&](cir::FuncOp func) {
    if (func.isDeclaration() || func.getBody().empty() ||
        !llvm::any_of(func.getFunctionType().getReturnTypes(),
                      [](mlir::Type T) {
                        return mlir::isa<cir::PointerType, cir::RecordType>(T);
                      }))
      return;
    Walker w(func, symbols, carries);
    func.walk([&](cir::ReturnOp ret) {
      for (mlir::Value v : ret.getOperands())
        w.value(v);
    });
    ReturnOriginFact fact;
    fact.function = func.getSymName().str();
    fact.args.assign(w.args.begin(), w.args.end());
    emit(fact);
  });
}

void cir::collectCaptures(
    mlir::ModuleOp module,
    llvm::function_ref<bool(cir::FuncOp, unsigned)> carries,
    llvm::function_ref<llvm::SmallVector<int>(cir::FuncOp, unsigned)>
        capturedBy,
    llvm::function_ref<void(const CaptureFact &)> emit) {
  mlir::SymbolTableCollection symbols;
  module.walk([&](cir::FuncOp func) {
    if (func.isDeclaration() || func.getBody().empty())
      return;
    llvm::MapVector<unsigned, llvm::SetVector<int>> by;
    // Every argument the stored value comes from is captured by every
    // argument (or global) the destination comes from.
    auto record = [&](Walker &stored, Walker &into) {
      for (unsigned arg : stored.args) {
        for (unsigned holder : into.args)
          if (holder != arg)
            by[arg].insert(holder);
        if (into.global)
          by[arg].insert(GlobalCapture);
      }
    };
    func.walk([&](mlir::Operation *op) {
      if (auto store = mlir::dyn_cast<cir::StoreOp>(op)) {
        if (slotOf(store.getAddr()))
          return;
        Walker stored(func, symbols, carries), into(func, symbols, carries);
        stored.value(store.getValue());
        into.value(store.getAddr());
        record(stored, into);
      } else if (auto copy = mlir::dyn_cast<cir::CopyOp>(op)) {
        if (slotOf(copy.getDst()))
          return;
        Walker stored(func, symbols, carries), into(func, symbols, carries);
        stored.contents(copy.getSrc());
        into.value(copy.getDst());
        record(stored, into);
      } else if (auto call = mlir::dyn_cast<cir::CallOp>(op)) {
        mlir::FlatSymbolRefAttr name = call.getCalleeAttr();
        auto callee =
            name ? symbols.lookupNearestSymbolFrom<cir::FuncOp>(call, name)
                 : cir::FuncOp();
        if (!callee)
          return;
        auto args = call.getArgOperands();
        for (unsigned i = 0; i < args.size(); ++i)
          for (int j : capturedBy(callee, i)) {
            Walker stored(func, symbols, carries), into(func, symbols, carries);
            stored.value(args[i]);
            stored.contents(args[i]);
            if (j == GlobalCapture)
              into.global = true;
            else if (j >= 0 && unsigned(j) < args.size())
              into.value(args[j]);
            record(stored, into);
          }
      }
    });
    for (auto &[arg, holders] : by) {
      CaptureFact fact;
      fact.function = func.getSymName().str();
      fact.arg = arg;
      fact.by.assign(holders.begin(), holders.end());
      emit(fact);
    }
  });
}
