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
// parameter, the return value slot, a local pointer) stands for every value
// stored into it, which over-approximates what that load can see; that is
// the safe direction here, since an extra origin only adds a diagnostic.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/Analysis/CIRReturnOrigins.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"

using namespace cir;

namespace {

class Walker {
public:
  explicit Walker(cir::FuncOp func) : entry(func.getBody().front()) {}

  llvm::SetVector<unsigned> args;

  void value(mlir::Value v) {
    if (!seen.insert(v).second)
      return;
    if (auto arg = mlir::dyn_cast<mlir::BlockArgument>(v)) {
      if (arg.getOwner() == &entry)
        args.insert(arg.getArgNumber());
      return;
    }
    mlir::Operation *op = v.getDefiningOp();
    if (auto load = mlir::dyn_cast<cir::LoadOp>(op)) {
      if (auto slot = load.getAddr().getDefiningOp<cir::AllocaOp>())
        for (mlir::Operation *user : slot->getUsers())
          if (auto store = mlir::dyn_cast<cir::StoreOp>(user))
            if (store.getAddr() == slot.getResult())
              value(store.getValue());
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
  mlir::Block &entry;
  llvm::DenseSet<mlir::Value> seen;
};

} // namespace

void cir::collectReturnOrigins(
    mlir::ModuleOp module,
    llvm::function_ref<void(const ReturnOriginFact &)> emit) {
  module.walk([&](cir::FuncOp func) {
    if (func.isDeclaration() ||
        !llvm::any_of(
            func.getFunctionType().getReturnTypes(),
            [](mlir::Type T) { return mlir::isa<cir::PointerType>(T); }))
      return;
    Walker w(func);
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
