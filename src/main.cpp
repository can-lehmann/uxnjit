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

#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "../modules/metajit.cpp/jitir.hpp"
#include "../modules/metajit.cpp/lowerllvm.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    return 1;
  }

  llvm::LLVMContext llvm_context;

  llvm::SMDiagnostic error;
  std::unique_ptr<llvm::Module> module = llvm::parseIRFile(argv[1], error, llvm_context);
  if (!module) {
    return 1;
  }

  module->print(llvm::outs(), nullptr);

  metajit::Context context;
  metajit::Allocator allocator;
  metajit::Section* section = new metajit::Section(context, allocator);
  
  llvm::Function* function = module->getFunction("uxn_eval");
  assert(function);

  metajit::LowerLLVM lower_llvm(function, section);

  return 0;
}