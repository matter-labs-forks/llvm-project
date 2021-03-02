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
  };
}

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
    bool MatchWrapper(SDValue N, SyncVMISelAddressMode &AM);
    bool MatchAddressBase(SDValue N, SyncVMISelAddressMode &AM);

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
}  // end anonymous namespace


/// MatchWrapper - Try to match SyncVMISD::Wrapper node into an addressing mode.
/// These wrap things that will resolve down into a symbol reference.  If no
/// match is possible, this returns true, otherwise it returns false.
bool SyncVMDAGToDAGISel::MatchWrapper(SDValue N, SyncVMISelAddressMode &AM) {
  return false;
}

/// MatchAddressBase - Helper for MatchAddress. Add the specified node to the
/// specified addressing mode without any further recursion.
bool SyncVMDAGToDAGISel::MatchAddressBase(SDValue N, SyncVMISelAddressMode &AM) {
  return false;
}

bool SyncVMDAGToDAGISel::MatchAddress(SDValue N, SyncVMISelAddressMode &AM) {
  return true;
}

/// SelectAddr - returns true if it is able pattern match an addressing mode.
/// It returns the operands which make up the maximal addressing mode it can
/// match by reference.
bool SyncVMDAGToDAGISel::SelectAddr(SDValue N,
                                    SDValue &Base, SDValue &Disp) {
  return true;
}

bool SyncVMDAGToDAGISel::
SelectInlineAsmMemoryOperand(const SDValue &Op, unsigned ConstraintID,
                             std::vector<SDValue> &OutOps) {
  return false;
}

bool SyncVMDAGToDAGISel::tryIndexedLoad(SDNode *N) {
  return true;
}

bool SyncVMDAGToDAGISel::tryIndexedBinOp(SDNode *Op, SDValue N1, SDValue N2,
                                         unsigned Opc8, unsigned Opc16) {
  return false;
}


void SyncVMDAGToDAGISel::Select(SDNode *Node) {
}
