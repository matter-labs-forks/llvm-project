//= SyncVMInstPrinter.h - Convert SyncVM MCInst to assembly syntax -*- C++ -*-//
//
// This class prints a SyncVM MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYNCVM_MCTARGETDESC_SYNCVMINSTPRINTER_H
#define LLVM_LIB_TARGET_SYNCVM_MCTARGETDESC_SYNCVMINSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"

namespace llvm {
class SyncVMInstPrinter : public MCInstPrinter {
public:
  SyncVMInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                    const MCRegisterInfo &MRI)
      : MCInstPrinter(MAI, MII, MRI) {}

  void printInst(const MCInst *MI, uint64_t Address, StringRef Annot,
                 const MCSubtargetInfo &STI, raw_ostream &O) override;

  // Autogenerated by tblgen.
  void printInstruction(const MCInst *MI, uint64_t Address, raw_ostream &O);
  bool printAliasInstr(const MCInst *MI, uint64_t Address, raw_ostream &O);
  void printCustomAliasOperand(const MCInst *MI, uint64_t Address,
                               unsigned OpIdx, unsigned PrintMethodIdx,
                               raw_ostream &O);
  static const char *getRegisterName(unsigned RegNo);

private:
  void printOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O,
                    const char *Modifier = nullptr);
  void printPCRelImmOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printCCOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printMemOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printStackOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  /// Print external address flag
  void printEAFOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  /// Print init flag
  void printInitOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O);
};
}

#endif
