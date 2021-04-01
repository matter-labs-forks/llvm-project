//===-- SyncVMISelDAGToDAG.cpp - A dag to dag inst selector for SyncVM ----===//
//
// This file defines an instruction selector for the SyncVM target.
//
//===----------------------------------------------------------------------===//

#include "SyncVM.h"
#include "SyncVMTargetMachine.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "syncvm-isel"

namespace {
struct SyncVMISelAddressMode {
  enum { RegBase, FrameIndexBase } BaseType = RegBase;

  struct { // This is really a union, discriminated by BaseType!
    SDValue Reg;
    int FrameIndex = 0;
  } Base;

  int16_t Disp = 0;
  const GlobalValue *GV = nullptr;
  const Constant *CP = nullptr;
  const BlockAddress *BlockAddr = nullptr;
  const char *ES = nullptr;
  int JT = -1;
  Align Alignment; // CP alignment.

  SyncVMISelAddressMode() = default;

  bool hasSymbolicDisplacement() const {
    return GV != nullptr || CP != nullptr || ES != nullptr || JT != -1;
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() {
    errs() << "SyncVMISelAddressMode " << this << '\n';
    if (BaseType == RegBase && Base.Reg.getNode() != nullptr) {
      errs() << "Base.Reg ";
      Base.Reg.getNode()->dump();
    } else if (BaseType == FrameIndexBase) {
      errs() << " Base.FrameIndex " << Base.FrameIndex << '\n';
    }
    errs() << " Disp " << Disp << '\n';
    if (GV) {
      errs() << "GV ";
      GV->dump();
    } else if (CP) {
      errs() << " CP ";
      CP->dump();
      errs() << " Align" << Alignment.value() << '\n';
    } else if (ES) {
      errs() << "ES ";
      errs() << ES << '\n';
    } else if (JT != -1)
      errs() << " JT" << JT << " Align" << Alignment.value() << '\n';
  }
#endif
};
} // namespace

/// SyncVMDAGToDAGISel - SyncVM specific code to select SyncVM machine
/// instructions for SelectionDAG operations.
///
namespace {
class SyncVMDAGToDAGISel : public SelectionDAGISel {
public:
  SyncVMDAGToDAGISel(SyncVMTargetMachine &TM, CodeGenOpt::Level OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

private:
  StringRef getPassName() const override {
    return "SyncVM DAG->DAG Pattern Instruction Selection";
  }

  bool MatchAddress(SDValue N, SyncVMISelAddressMode &AM);

  bool SelectInlineAsmMemoryOperand(const SDValue &Op, unsigned ConstraintID,
                                    std::vector<SDValue> &OutOps) override;

  // Include the pieces autogenerated from the target description.
#include "SyncVMGenDAGISel.inc"

  // Main method to transform nodes into machine nodes.
  void Select(SDNode *N) override;

  bool tryIndexedLoad(SDNode *Op);
  bool tryIndexedBinOp(SDNode *Op, SDValue N1, SDValue N2, unsigned Opc8,
                       unsigned Opc16);

  bool SelectAddr(SDValue Addr, SDValue &Base, SDValue &Disp);
};
} // end anonymous namespace

bool SyncVMDAGToDAGISel::MatchAddress(SDValue N, SyncVMISelAddressMode &AM) {
  LLVM_DEBUG(errs() << "MatchAddress: "; AM.dump());

  switch (N.getOpcode()) {
  default:
    break;
  case ISD::Constant: {
    uint64_t Val = cast<ConstantSDNode>(N)->getSExtValue();
    AM.Disp += Val;
    return false;
  }
  case ISD::FrameIndex:
    if (AM.BaseType == SyncVMISelAddressMode::RegBase &&
        AM.Base.Reg.getNode() == nullptr) {
      AM.BaseType = SyncVMISelAddressMode::FrameIndexBase;
      AM.Base.FrameIndex = cast<FrameIndexSDNode>(N)->getIndex();
      return false;
    }
    break;
  case ISD::TargetGlobalAddress:
    auto *G = cast<GlobalAddressSDNode>(N);
    AM.GV = G->getGlobal();
    AM.Disp += G->getOffset();
    return false;
  }
  return true;
}

/// SelectAddr - returns true if it is able pattern match an addressing mode.
/// It returns the operands which make up the maximal addressing mode it can
/// match by reference.
bool SyncVMDAGToDAGISel::SelectAddr(SDValue N, SDValue &Base, SDValue &Disp) {
  SyncVMISelAddressMode AM;

  if (MatchAddress(N, AM))
    return false;

  // TODO: Hack (constant is used to designate immediate addressing mode),
  // redesign.
  if (AM.BaseType == SyncVMISelAddressMode::RegBase)
    if (!AM.Base.Reg.getNode())
      AM.Base.Reg = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i256);

  Base = (AM.BaseType == SyncVMISelAddressMode::FrameIndexBase)
             ? CurDAG->getTargetFrameIndex(
                   AM.Base.FrameIndex,
                   getTargetLowering()->getPointerTy(CurDAG->getDataLayout()))
             : AM.Base.Reg;
  if (AM.GV)
    Disp = CurDAG->getTargetGlobalAddress(AM.GV, SDLoc(N), MVT::i256, AM.Disp,
                                          0 /*AM.SymbolFlags*/);
  else
    Disp = CurDAG->getTargetConstant(AM.Disp, SDLoc(N), MVT::i16);

  return true;
}

bool SyncVMDAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, unsigned ConstraintID, std::vector<SDValue> &OutOps) {
  return false;
}

bool SyncVMDAGToDAGISel::tryIndexedLoad(SDNode *N) { return true; }

bool SyncVMDAGToDAGISel::tryIndexedBinOp(SDNode *Op, SDValue N1, SDValue N2,
                                         unsigned Opc8, unsigned Opc16) {
  return false;
}

void SyncVMDAGToDAGISel::Select(SDNode *Node) { SelectCode(Node); }

/// createSyncVMISelDag - This pass converts a legalized DAG into a
/// SyncVM-specific DAG, ready for instruction scheduling.
///
FunctionPass *llvm::createSyncVMISelDag(SyncVMTargetMachine &TM,
                                        CodeGenOpt::Level OptLevel) {
  return new SyncVMDAGToDAGISel(TM, OptLevel);
}
