#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

namespace {

class FmaCombinePass : public MachineFunctionPass {
public:
  static char ID;
  FmaCombinePass() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    const X86Subtarget &ST = MF.getSubtarget<X86Subtarget>();
    if (!ST.hasFMA())
      return false;
    const X86InstrInfo *TII = ST.getInstrInfo();
    MachineRegisterInfo &MRI = MF.getRegInfo();
    bool Changed = false;

    for (auto &MBB : MF)
      Changed |= combineInBlock(MBB, TII, MRI);

    return Changed;
  }

private:
  bool combineInBlock(MachineBasicBlock &MBB,
                      const X86InstrInfo *TII,
                      MachineRegisterInfo &MRI) {
    for (auto MI = MBB.begin(), ME = MBB.end(); MI != ME; ++MI) {
      unsigned SubOpc = MI->getOpcode();
      if (!isSubInstr(SubOpc))
        continue;

      Register dst = MI->getOperand(0).getReg();
      Register c   = MI->getOperand(1).getReg();
      Register tmp = MI->getOperand(2).getReg();

      MachineInstr *MulMI = MRI.getUniqueVRegDef(tmp);
      if (!MulMI || !isMulInstr(MulMI->getOpcode()))
        continue;

      Register a = MulMI->getOperand(1).getReg();
      Register b = MulMI->getOperand(2).getReg();

      unsigned FmaOpc = fmaOpcodeFor(SubOpc);
      if (!FmaOpc)
        continue;

      BuildMI(MBB, *MulMI, MulMI->getDebugLoc(), TII->get(FmaOpc), dst)
        .addReg(a)
        .addReg(b)
        .addReg(c)
        .addImm(0)
        .addReg(X86::MXCSR, RegState::Implicit)
        .addReg(X86::MXCSR, RegState::Implicit);

      MI->eraseFromParent();
      MulMI->eraseFromParent();
      return true;
    }
    return false;
  }

static bool isMulInstr(unsigned Opc) {
  switch (Opc) {
  case X86::MULSSrr: case X86::MULSSrm:
  case X86::MULPSrr: case X86::MULPSrm:
  case X86::MULSDrr: case X86::MULSDrm:
  case X86::MULPDrr: case X86::MULPDrm:
    return true;
  default:
    return false;
  }
}

static bool isSubInstr(unsigned Opc) {
  switch (Opc) {
  case X86::SUBSSrr: case X86::SUBSSrm:
  case X86::SUBPSrr: case X86::SUBPSrm:
  case X86::SUBSDrr:
  case X86::SUBPDrr:
    return true;
  default:
    return false;
  }
}

  static unsigned fmaOpcodeFor(unsigned SubOpc) {
    switch (SubOpc) {
    case X86::SUBSSrr: return X86::VFMSUB213SSr;
    case X86::SUBPSrr: return X86::VFMSUB213PSr;
    case X86::SUBSDrr: return X86::VFMSUB213SDr;
    case X86::SUBPDrr: return X86::VFMSUB213PDYr;
    default:           return 0;
    }
  }
};

char FmaCombinePass::ID = 0;
static RegisterPass<FmaCombinePass>
    X("fma-combine", "Fuse multiply and subtract into FMA", false, false);

} // namespace
