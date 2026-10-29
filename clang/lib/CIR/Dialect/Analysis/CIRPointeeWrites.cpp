//===- CIRPointeeWrites.cpp - Is a pointer parameter written through -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A flow-insensitive walk over the uses of each pointer parameter. Every use
// must be known to only read; anything else, including an op this file does
// not know, counts as a write. A copy of the pointer stored anywhere, even a
// local, counts as an escape: the copy's declared type would have to change
// too.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/Analysis/CIRPointeeWrites.h"
#include "llvm/ADT/DenseSet.h"

using namespace cir;

namespace {

class Walker {
public:
  Walker(llvm::function_ref<PointerArgUse(cir::FuncOp, unsigned)> argUse,
         mlir::SymbolTableCollection &symbols, PointeeWriteFact &fact)
      : argUse(argUse), symbols(symbols), fact(fact) {}

  void param(mlir::BlockArgument arg) {
    for (mlir::OpOperand &use : arg.getUses()) {
      // CIRGen spills each parameter to its own slot; the loads of that slot
      // are the parameter's value.
      auto store = mlir::dyn_cast<cir::StoreOp>(use.getOwner());
      auto slot = store && store.getValue() == arg
                      ? store.getAddr().getDefiningOp<cir::AllocaOp>()
                      : cir::AllocaOp();
      if (!slot) {
        value(arg);
        continue;
      }
      for (mlir::Operation *user : slot->getUsers()) {
        if (auto load = mlir::dyn_cast<cir::LoadOp>(user))
          value(load.getResult());
        else if (!mlir::isa<cir::StoreOp>(user) ||
                 mlir::cast<cir::StoreOp>(user).getAddr() != slot.getResult())
          write("address of the parameter taken");
      }
    }
  }

private:
  void write(llvm::StringRef why) {
    if (!fact.written) {
      fact.written = true;
      fact.why = why.str();
    }
  }

  void value(mlir::Value v) {
    if (!seen.insert(v).second)
      return;
    for (mlir::OpOperand &use : v.getUses())
      operand(use);
  }

  void operand(mlir::OpOperand &use) {
    mlir::Operation *op = use.getOwner();
    // ptr_diff and objsize read only the address.
    if (mlir::isa<cir::LoadOp, cir::CmpOp, cir::PtrDiffOp, cir::ObjSizeOp>(op))
      return;
    if (mlir::isa<cir::MemCpyOp, cir::MemMoveOp>(op)) {
      mlir::Value dst = mlir::isa<cir::MemCpyOp>(op)
                            ? mlir::cast<cir::MemCpyOp>(op).getDst()
                            : mlir::cast<cir::MemMoveOp>(op).getDst();
      if (dst == use.get())
        write("memcpy or memmove destination");
      return;
    }
    if (auto store = mlir::dyn_cast<cir::StoreOp>(op))
      return write(store.getAddr() == use.get() ? "stored through"
                                                : "copied");
    if (mlir::isa<cir::GetMemberOp, cir::GetElementOp, cir::PtrStrideOp,
                  cir::BaseClassAddrOp, cir::DerivedClassAddrOp>(op)) {
      for (mlir::Value result : op->getResults())
        if (mlir::isa<cir::PointerType>(result.getType()))
          value(result);
      return;
    }
    if (auto cast = mlir::dyn_cast<cir::CastOp>(op)) {
      if (mlir::isa<cir::PointerType>(cast.getType()))
        return value(cast.getResult());
      if (cast.getKind() != cir::CastKind::ptr_to_bool)
        write("converted to an integer");
      return;
    }
    if (auto copy = mlir::dyn_cast<cir::CopyOp>(op)) {
      if (copy.getDst() == use.get())
        write("copied over");
      return;
    }
    if (auto call = mlir::dyn_cast<cir::CallOp>(op))
      return callArg(call, use);
    write(("used by " + op->getName().getStringRef()).str());
  }

  void callArg(cir::CallOp call, mlir::OpOperand &use) {
    mlir::FlatSymbolRefAttr name = call.getCalleeAttr();
    auto callee =
        name ? symbols.lookupNearestSymbolFrom<cir::FuncOp>(call, name)
             : cir::FuncOp();
    if (!callee)
      return write("passed to an unknown callee");
    for (auto [index, arg] : llvm::enumerate(call.getArgOperands())) {
      if (arg != use.get())
        continue;
      switch (argUse(callee, index)) {
      case PointerArgUse::ReadOnly:
        break;
      case PointerArgUse::Writes:
        write("passed to a parameter that may write");
        break;
      case PointerArgUse::Forward:
        fact.forwards.push_back({callee.getSymName().str(),
                                 static_cast<unsigned>(index)});
        break;
      }
    }
  }

  llvm::function_ref<PointerArgUse(cir::FuncOp, unsigned)> argUse;
  mlir::SymbolTableCollection &symbols;
  PointeeWriteFact &fact;
  llvm::DenseSet<mlir::Value> seen;
};

} // namespace

void cir::collectPointeeWrites(
    mlir::ModuleOp module,
    llvm::function_ref<PointerArgUse(cir::FuncOp, unsigned)> argUse,
    llvm::function_ref<void(const PointeeWriteFact &)> emit) {
  mlir::SymbolTableCollection symbols;
  module.walk([&](cir::FuncOp func) {
    if (func.isDeclaration())
      return;
    for (mlir::BlockArgument arg : func.getBody().front().getArguments()) {
      if (!mlir::isa<cir::PointerType>(arg.getType()))
        continue;
      PointeeWriteFact fact;
      fact.function = func.getSymName().str();
      fact.index = arg.getArgNumber();
      Walker(argUse, symbols, fact).param(arg);
      emit(fact);
    }
  });
}
