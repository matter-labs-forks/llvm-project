//===-- SyncVMTargetMachine.cpp - Define TargetMachine for SyncVM ---------===//
//
// Top-level implementation for the SyncVM target.
//
//===----------------------------------------------------------------------===//

#include "SyncVMTargetMachine.h"
#include "SyncVM.h"
#include "TargetInfo/SyncVMTargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSyncVMTarget() {
  // Register the target.
  RegisterTargetMachine<SyncVMTargetMachine> X(getTheSyncVMTarget());
}

static std::string computeDataLayout() { return "e-p:16:8-i256:256:256"; }

static Reloc::Model getEffectiveRelocModel(Optional<Reloc::Model> RM) {
  if (!RM.hasValue())
    return Reloc::Static;
  return *RM;
}

SyncVMTargetMachine::SyncVMTargetMachine(const Target &T, const Triple &TT,
                                         StringRef CPU, StringRef FS,
                                         const TargetOptions &Options,
                                         Optional<Reloc::Model> RM,
                                         Optional<CodeModel::Model> CM,
                                         CodeGenOpt::Level OL, bool JIT)
    : LLVMTargetMachine(T, computeDataLayout(), TT, CPU, FS, Options,
                        getEffectiveRelocModel(RM),
                        getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
    setRequiresStructuredCFG(true);
    initAsmInfo();
}

SyncVMTargetMachine::~SyncVMTargetMachine() {}

namespace {
/// SyncVM Code Generator Pass Configuration Options.
class SyncVMPassConfig : public TargetPassConfig {
public:
  SyncVMPassConfig(SyncVMTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  SyncVMTargetMachine &getSyncVMTargetMachine() const {
    return getTM<SyncVMTargetMachine>();
  }

  bool addInstSelector() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *SyncVMTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new SyncVMPassConfig(*this, PM);
}

bool SyncVMPassConfig::addInstSelector() { return false; }

void SyncVMPassConfig::addPreEmitPass() {}

