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
  enum { RegBase, FrameIndexBase, StackRegBase } BaseType = RegBase;

  struct {
    SDValue Reg;
    int FrameIndex = 0;
  } Base;

  int16_t Disp = 0;
  const GlobalValue *GV = nullptr;
  bool NeedsAdjustment = false;

  bool isDefault() const {
    return !BaseType && !Base.Reg.getNode() && !Base.FrameIndex && !Disp && !GV;
  }

  bool isOnStack() const { return BaseType != RegBase; }

  SyncVMISelAddressMode() = default;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() {
    errs() << "SyncVMISelAddressMode " << this << '\n';
    if (Base.Reg.getNode() != nullptr) {
      errs() << "Base.Reg ";
      Base.Reg.getNode()->dump();
    }
    if (BaseType == FrameIndexBase) {
      errs() << " Base.FrameIndex " << Base.FrameIndex << '\n';
    }
    errs() << " Disp " << Disp << '\n';
    if (GV) {
      errs() << "GV ";
      GV->dump();
    }
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

  bool MatchAddress(SDValue N, SyncVMISelAddressMode &AM, bool IsStackAddr);
  bool MatchAddressBase(SDValue N, SyncVMISelAddressMode &AM, bool IsStackAddr);

  // Include the pieces autogenerated from the target description.
#include "SyncVMGenDAGISel.inc"

  // Main method to transform nodes into machine nodes.
  void Select(SDNode *N) override;

  bool SelectMemAddr(SDValue Addr, SDValue &Base, SDValue &Disp);
  bool SelectStackAddr(SDValue Addr, SDValue &Base1, SDValue &Base2,
                       SDValue &Disp);
  bool SelectAdjStackAddr(SDValue Addr, SDValue &Base1, SDValue &Base2,
                          SDValue &Disp);
  bool SelectStackAddrCommon(SDValue Addr, SDValue &Base1, SDValue &Base2,
                             SDValue &Disp, bool IsAdjusted);
  SyncVMISelAddressMode MergeAddr(const SyncVMISelAddressMode &LHS,
                                  const SyncVMISelAddressMode &RHS, SDLoc DL);
};
} // end anonymous namespace

/// Merge \p LHS and \p RHS address modes as if they are added together.
/// The main goal is to transform patterns like (+ (+ fi reg), (* reg ci)) to
/// (+ (+ fi ci), NewReg) so SyncVM can select it.
SyncVMISelAddressMode
SyncVMDAGToDAGISel::MergeAddr(const SyncVMISelAddressMode &LHS,
                              const SyncVMISelAddressMode &RHS, SDLoc DL) {
  SyncVMISelAddressMode Result;
  Result.BaseType = SyncVMISelAddressMode::FrameIndexBase;
  Result.Base.FrameIndex = LHS.Base.FrameIndex | RHS.Base.FrameIndex;
  if (LHS.Base.Reg.getNode() && RHS.Base.Reg.getNode()) {
    Result.Base.Reg =
        CurDAG->getNode(ISD::ADD, DL, MVT::i256, LHS.Base.Reg, RHS.Base.Reg);
    SelectCode(Result.Base.Reg.getNode());
  } else if (LHS.Base.Reg.getNode()) {
    Result.Base.Reg = LHS.Base.Reg;
  } else if (RHS.Base.Reg.getNode()) {
    Result.Base.Reg = RHS.Base.Reg;
  }
  Result.Disp += LHS.Disp + RHS.Disp;
  return Result;
}

/// MatchAddressBase - Helper for MatchAddress. Add the specified node to the
/// specified addressing mode without any further recursion.
bool SyncVMDAGToDAGISel::MatchAddressBase(SDValue N, SyncVMISelAddressMode &AM,
                                          bool IsStackAddr) {
  // Is the base register already occupied?
  if ((!IsStackAddr && AM.BaseType != SyncVMISelAddressMode::RegBase) ||
      AM.Base.Reg.getNode()) {
    // If so, we cannot select it.
    return true;
  }

  // Default, generate it as a register.
  AM.Base.Reg = N;
  return false;
}

bool SyncVMDAGToDAGISel::MatchAddress(SDValue N, SyncVMISelAddressMode &AM,
                                      bool IsStackAddr) {
  LLVM_DEBUG(errs() << "MatchAddress: "; AM.dump());

  if (N.isMachineOpcode() && N.getMachineOpcode() == SyncVM::AdjSP) {
    assert(IsStackAddr && "Unexpected AdjSP in non-stack addressing");
    // The offset is in a register, no frame index involved
    AM.BaseType = SyncVMISelAddressMode::StackRegBase;
    AM.NeedsAdjustment = true;
    return MatchAddressBase(N.getOperand(0), AM, IsStackAddr);
  }

  switch (N.getOpcode()) {
  default: {
    break;
  }
  case ISD::Constant: {
    uint64_t Val = cast<ConstantSDNode>(N)->getSExtValue();
    AM.Disp += Val;
    return false;
  }
  case ISD::FrameIndex: {
    if (!IsStackAddr)
      return true;
    if (IsStackAddr && AM.BaseType == SyncVMISelAddressMode::RegBase) {
      AM.BaseType = SyncVMISelAddressMode::FrameIndexBase;
      AM.Base.FrameIndex = cast<FrameIndexSDNode>(N)->getIndex();
      return false;
    }
    break;
  }
  case ISD::TargetGlobalAddress: {
    auto *G = cast<GlobalAddressSDNode>(N);
    AM.GV = G->getGlobal();
    AM.Disp += G->getOffset();
    // Ext loads, trunc stores has offset in bits
    if ((isa<StoreSDNode>(N) && cast<StoreSDNode>(N)->isTruncatingStore()) ||
        (isa<LoadSDNode>(N) &&
         cast<LoadSDNode>(N)->getExtensionType() != ISD::NON_EXTLOAD))
      AM.Disp /= 8;
    return false;
  }
  case ISD::ADD: {
    SyncVMISelAddressMode Backup = AM;
    const SDValue &Operand0 = N.getNode()->getOperand(0);
    const SDValue &Operand1 = N.getNode()->getOperand(1);
    if (!MatchAddress(Operand0, AM, IsStackAddr) &&
        !MatchAddress(Operand1, AM, IsStackAddr))
      return false;
    AM = Backup;
    if (!MatchAddress(Operand1, AM, IsStackAddr) &&
        !MatchAddress(Operand0, AM, IsStackAddr))
      return false;
    AM = Backup;
    if (IsStackAddr)
      AM.BaseType = SyncVMISelAddressMode::StackRegBase;
    break;
  }
  case ISD::OR:
    // Handle "X | C" as "X + C" iff X is known to have C bits clear.
    if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
      SyncVMISelAddressMode Backup = AM;
      uint64_t Offset = CN->getSExtValue();
      // Start with the LHS as an addr mode.
      if (!MatchAddress(N.getOperand(0), AM, IsStackAddr) &&
          // Address could not have picked a GV address for the displacement.
          AM.GV == nullptr &&
          // Check to see if the LHS & C is zero.
          CurDAG->MaskedValueIsZero(N.getOperand(0), CN->getAPIntValue())) {
        AM.Disp += Offset;
        return false;
      }
      AM = Backup;
    }
    break;
  case ISD::CopyFromReg:
    if (IsStackAddr)
      // The offset is in a register, no frame index involved
      AM.BaseType = SyncVMISelAddressMode::StackRegBase;
    break;
  case ISD::AssertZext:
    return MatchAddress(N->getOperand(0), AM, IsStackAddr);
  }
  return MatchAddressBase(N, AM, IsStackAddr);
}

/// SelectMemAddr - returns true if it is able pattern match an addressing mode
/// for heap, parent or child memory. It returns the operands which make up the
/// maximal addressing mode it can match by reference.
bool SyncVMDAGToDAGISel::SelectMemAddr(SDValue N, SDValue &Base,
                                       SDValue &Disp) {
  SyncVMISelAddressMode AM;

  if (MatchAddress(N, AM, false /* IsStackAddr */)) {
    LLVM_DEBUG(errs() << "Failed to match address.");
    return false;
  } else {
    LLVM_DEBUG(errs() << "Matched: "; AM.dump());
  }

  // TODO: Hack (constant is used to designate immediate addressing mode),
  // redesign.
  assert(AM.BaseType == SyncVMISelAddressMode::RegBase);
  if (!AM.Base.Reg.getNode())
    AM.Base.Reg = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i256);

  if (AM.Base.Reg.getOpcode() != ISD::TargetConstant) {
    SDValue &Reg = AM.Base.Reg;
    if (Reg.getOpcode() == ISD::MUL &&
        Reg.getOperand(1).getOpcode() == ISD::Constant &&
        cast<ConstantSDNode>(Reg.getOperand(1))->getSExtValue() == 32)
      AM.Base.Reg = Reg.getOperand(0);
    else {
      auto ConstMaterialize = CurDAG->getMachineNode(
          SyncVM::CONST, SDLoc(N), MVT::i256,
          CurDAG->getTargetConstant(32, SDLoc(N), MVT::i256));
      auto AddrNode =
          CurDAG->getMachineNode(SyncVM::DIVrrrz, SDLoc(N), MVT::i256,
                                 AM.Base.Reg, SDValue(ConstMaterialize, 0));
      AM.Base.Reg = SDValue(AddrNode, 0);
    }
  }

  Base = AM.Base.Reg;

  if (AM.GV)
    Disp = CurDAG->getTargetGlobalAddress(AM.GV, SDLoc(N), MVT::i256, AM.Disp,
                                          0 /*AM.SymbolFlags*/);
  else
    Disp = CurDAG->getTargetConstant(AM.Disp, SDLoc(N), MVT::i16);

  return true;
}

/// SelectStackAddr - returns true if it is able pattern match an addressing
/// mode for stack. It returns the operands which make up the maximal addressing
/// mode it can match by reference.
bool SyncVMDAGToDAGISel::SelectStackAddrCommon(SDValue N, SDValue &Base1,
                                               SDValue &Base2, SDValue &Disp,
                                               bool IsAdjusted) {
  SyncVMISelAddressMode AM;

  if (MatchAddress(N, AM, true /* IsStackAddr */)) {
    LLVM_DEBUG(errs() << "Failed to match address.");
    return false;
  } else {
    LLVM_DEBUG(errs() << "Matched: "; AM.dump());
  }

  if (!AM.isOnStack())
    return false;

  if (AM.NeedsAdjustment != IsAdjusted)
    return false;

  // TODO: Hack (constant is used to designate immediate addressing mode),
  // redesign.
  if (!AM.Base.Reg.getNode())
    AM.Base.Reg = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i256);
  if (AM.Base.Reg.getOpcode() != ISD::TargetConstant) {
    SDValue &Reg = AM.Base.Reg;
    if (Reg.getOpcode() == ISD::MUL &&
        Reg.getOperand(1).getOpcode() == ISD::Constant &&
        cast<ConstantSDNode>(Reg.getOperand(1))->getSExtValue() == 32)
      AM.Base.Reg = Reg.getOperand(0);
    else {
      auto ConstMaterialize = CurDAG->getMachineNode(
          SyncVM::CONST, SDLoc(N), MVT::i256,
          CurDAG->getTargetConstant(32, SDLoc(N), MVT::i256));
      auto AddrNode =
          CurDAG->getMachineNode(SyncVM::DIVrrrz, SDLoc(N), MVT::i256,
                                 AM.Base.Reg, SDValue(ConstMaterialize, 0));
      AM.Base.Reg = SDValue(AddrNode, 0);
    }
  }

  Base1 = (AM.BaseType == SyncVMISelAddressMode::FrameIndexBase)
              ? CurDAG->getTargetFrameIndex(
                    AM.Base.FrameIndex,
                    getTargetLowering()->getPointerTy(CurDAG->getDataLayout()))
              : CurDAG->getRegister(SyncVM::SP, MVT::i256);
  Base2 = AM.Base.Reg;

  // 1(sp) is the index of the 1st element on the stack rather than 0(sp).
  AM.Disp += 32;

  if (AM.GV)
    Disp = CurDAG->getTargetGlobalAddress(AM.GV, SDLoc(N), MVT::i256, AM.Disp,
                                          0 /*AM.SymbolFlags*/);
  else
    Disp = CurDAG->getTargetConstant(AM.Disp, SDLoc(N), MVT::i16);

  return true;
}

bool SyncVMDAGToDAGISel::SelectStackAddr(SDValue N, SDValue &Base1,
                                         SDValue &Base2, SDValue &Disp) {
  return SelectStackAddrCommon(N, Base1, Base2, Disp, false);
}

bool SyncVMDAGToDAGISel::SelectAdjStackAddr(SDValue N, SDValue &Base1,
                                            SDValue &Base2, SDValue &Disp) {
  return SelectStackAddrCommon(N, Base1, Base2, Disp, true);
}

void SyncVMDAGToDAGISel::Select(SDNode *Node) {
  SDLoc DL(Node);

  // If we have a custom node, we already have selected!
  if (Node->isMachineOpcode()) {
    LLVM_DEBUG(errs() << "== "; Node->dump(CurDAG); errs() << "\n");
    Node->setNodeId(-1);
    return;
  }

  // Few custom selection stuff.
  switch (Node->getOpcode()) {
  default:
    break;
  case ISD::FrameIndex: {
    assert(Node->getValueType(0) == MVT::i256);
    int FI = cast<FrameIndexSDNode>(Node)->getIndex();
    SDValue TFI = CurDAG->getTargetFrameIndex(FI, MVT::i256);
    if (Node->hasOneUse()) {
      CurDAG->SelectNodeTo(Node, SyncVM::ADDframe, MVT::i256, TFI,
                           CurDAG->getTargetConstant(0, DL, MVT::i256));
      return;
    }
    ReplaceNode(Node, CurDAG->getMachineNode(
                          SyncVM::ADDframe, DL, MVT::i256, TFI,
                          CurDAG->getTargetConstant(0, DL, MVT::i256)));
    return;
  }
  }

  // Select the default instruction
  SelectCode(Node);
}

/// createSyncVMISelDag - This pass converts a legalized DAG into a
/// SyncVM-specific DAG, ready for instruction scheduling.
///
FunctionPass *llvm::createSyncVMISelDag(SyncVMTargetMachine &TM,
                                        CodeGenOpt::Level OptLevel) {
  return new SyncVMDAGToDAGISel(TM, OptLevel);
}
