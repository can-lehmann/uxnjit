// Copyright 2025 Can Joshua Lehmann
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <iostream>
#include <memory>
#include <fstream>

#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"

#include "../modules/metajit.cpp/jitir.hpp"
#include "../modules/metajit.cpp/lowerllvm.hpp"
#include "../modules/metajit.cpp/llvmgen.hpp"

#include "uxn.h"

int main(int argc, char** argv) {
  if (argc != 3) {
    return 1;
  }

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();

  llvm::LLVMContext llvm_context;

  llvm::SMDiagnostic error;
  std::unique_ptr<llvm::Module> source_module = llvm::parseIRFile(argv[1], error, llvm_context);
  if (!source_module) {
    return 1;
  }

  metajit::Context context;
  metajit::Allocator allocator;
  metajit::Section* section = new metajit::Section(context, allocator);
  
  llvm::Function* function = source_module->getFunction("uxn_eval");
  assert(function);
  metajit::LowerLLVM lower_llvm(function, section);

  if (section->verify(std::cerr)) {
    return 1;
  }

  metajit::DeadCodeElim::run(section);
  metajit::Simplify::run(section, 5);
  metajit::CommonSubexprElim::run(section);
  metajit::DeadCodeElim::run(section);
  section->write(std::cerr);

  if (section->verify(std::cerr)) {
    return 1;
  }

  std::unique_ptr<llvm::Module> module = std::make_unique<llvm::Module>("module", llvm_context);
  metajit::LLVMCodeGen::run(section, module.get(), "step", false);
  metajit::LLVMCodeGen::run(section, module.get(), "trace", true);

  llvm::ExitOnError ExitOnErr;

  std::unique_ptr<llvm::orc::LLJIT> jit = ExitOnErr(llvm::orc::LLJITBuilder().create());

  ExitOnErr(metajit::map_symbols(*jit));

  ExitOnErr(jit->addIRModule(llvm::orc::ThreadSafeModule(
    std::move(module),
    std::make_unique<llvm::LLVMContext>()
  )));

  using StepFn = void(*)(uint8_t*);
  using TraceFn = void(*)(uint8_t*, metajit::TraceBuilder*);
  StepFn step = ExitOnErr(jit->lookup("step")).toPtr<StepFn>();
  TraceFn trace = ExitOnErr(jit->lookup("trace")).toPtr<TraceFn>();

  constexpr size_t ROM_SIZE = 0x10000;

  uint8_t* counts = new uint8_t[ROM_SIZE]();


  Uxn uxn = {0};
  uxn.ram = new uint8_t[ROM_SIZE]();
  uxn.pc = 0x100;

  std::ifstream rom_file(argv[2], std::ios::binary);
  size_t size = 0;
  rom_file.seekg(0, std::ios::end);
  size = rom_file.tellg();
  rom_file.seekg(0, std::ios::beg);
  if (size + 0x100 > ROM_SIZE) {
    return 1;
  }
  rom_file.read((char*) &uxn.ram[0x100], size); 

  metajit::Allocator trace_allocator;
  
  while (uxn.pc) {
    counts[uxn.pc]++;
    if (counts[uxn.pc] > 5) {
      std::cerr << "Tracing at PC " << std::hex << uxn.pc << std::dec << "\n";
      metajit::Section* trace_section = new metajit::Section(context, trace_allocator);
      metajit::TraceBuilder trace_builder(trace_section);

      trace_builder.build_input(metajit::Type::Ptr); // Uxn*
      trace_builder.move_to_end(trace_builder.build_block());

      uint16_t start_pc = uxn.pc;
      for (size_t it = 0; it < 64; it++) {
        trace((uint8_t*) &uxn, &trace_builder);
        if (uxn.pc == start_pc) {
          break;
        }
      }

      if (uxn.pc != start_pc) {
        trace_builder.build_exit();
      } else {
        trace_builder.build_jump(*trace_section->begin());
      }
      
      trace_section->write(std::cerr);

      if (trace_section->verify(std::cerr)) {
        return 1;
      }

      exit(0);
    } else {
      step((uint8_t*) &uxn);
    }
  }

  return 0;
}