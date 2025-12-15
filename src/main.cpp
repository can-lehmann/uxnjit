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

namespace metajit {
  class AliasingFromStructure: public Pass<AliasingFromStructure> {
  public:
    class Structure {
    private:
      AliasingGroup _group = 0;
    public:
      Structure(AliasingGroup group): _group(group) {}
      virtual ~Structure() {}

      virtual AliasingGroup group() const { return _group; }  
    };

    class Field {
    private:
      size_t _offset = 0;
      Structure* _structure = nullptr;
      AliasingGroup _exact_group = 0;
    public:
      Field() {}
      Field(size_t offset, Structure* structure, AliasingGroup exact_group):
        _offset(offset), _structure(structure), _exact_group(exact_group) {}
      
      size_t offset() const { return _offset; }
      Structure* structure() const { return _structure; }
      AliasingGroup exact_group() const { return _exact_group; }
      bool has_exact_group() const { return _exact_group < 0; }
    };

    class Record: public Structure {
    private:
      std::map<size_t, Field> _fields;
    public:
      Record(AliasingGroup group, const std::vector<Field>& fields):
          Structure(group) {
        for (const Field& field : fields) {
          _fields[field.offset()] = field;
        }
      }

      const Field* field(size_t offset) const {
        if (_fields.find(offset) != _fields.end()) {
          return &_fields.at(offset);
        } else {
          return nullptr;
        }
      }
    };

    class Array: public Structure {
    private:
      Structure* _element;
    public:
      Array(AliasingGroup group, Structure* element):
        Structure(group), _element(element) {}
      
      Structure* element() const { return _element; }
    };

    class StructureBuilder {
    private:
      AliasingGroup _exact_group = -1;
      AliasingGroup _group = 0;
      std::vector<Structure*> _structures;

      Structure* add(Structure* structure) {
        _structures.push_back(structure);
        return structure;
      }
    public:
      StructureBuilder() {}
      ~StructureBuilder() {
        for (Structure* structure : _structures) {
          delete structure;
        }
      }

      Field field(size_t offset, Structure* structure) {
        return Field(offset, structure, _exact_group--);
      }

      Structure* array(Structure* element = nullptr) {
        return add(new Array(_group++, element));
      }

      Structure* record(const std::vector<Field>& fields) {
        return add(new Record(_group++, fields));
      }
    };
  private:
    bool _strict_mode = true;
    std::vector<Structure*> _inputs;
    InstMap<Structure*> _structs;

    Structure* at(Value* value) {
      if (value->is_inst()) {
        return _structs[(Inst*) value];
      } else if (dynmatch(Input, input, value)) {
        return _inputs[input->index()];
      } else {
        return nullptr;
      }
    }

    void apply(Section* section) {
      assert(_inputs.size() == section->inputs().size());

      _structs.init(section);

      for (Block* block : *section) {
        for (Inst* inst : *block) {
          if (dynmatch(LoadInst, load, inst)) {
            Structure* structure = at(load->ptr());
            if (!structure) {
              throw std::runtime_error("AliasingFromStructure: Load from unknown structure");
            }

            load->set_aliasing(structure->group());

            if (dynmatch(Array, array, structure)) {
              _structs[inst] = array->element();
            } else if (dynmatch(Record, record, structure)) {
              const Field* field = record->field(load->offset());
              if (field) {
                _structs[inst] = field->structure();
                if (field->has_exact_group()) {
                  load->set_aliasing(field->exact_group());
                }
              } else {
                _structs[inst] = nullptr;
              }
            } else {
              assert(false); // Unreachable
            }
          } else if (dynmatch(StoreInst, store, inst)) {
            Structure* structure = at(store->ptr());
            if (!structure) {
              throw std::runtime_error("AliasingFromStructure: Store to unknown structure");
            }

            store->set_aliasing(structure->group());

            if (dynmatch(Record, record, structure)) {
              const Field* field = record->field(store->offset());
              if (field && field->has_exact_group()) {
                store->set_aliasing(field->exact_group());
              }
            }
          } else if (dynmatch(AddPtrInst, add_ptr, inst)) {
            if (dynmatch(Array, array, at(add_ptr->ptr()))) {
              _structs[inst] = at(add_ptr->ptr());
            }
          } else if (dynamic_cast<AssumeConstInst*>(inst) ||
                     dynamic_cast<FreezeInst*>(inst)) {
            _structs[inst] = at(inst->arg(0));
          }

          if (_strict_mode && !_structs[inst] && inst->type() == Type::Ptr) {
            std::ostringstream stream;
            stream << "AliasingFromStructure: Unable to determine structure for instruction\n";
            inst->write(stream);
            throw std::runtime_error(stream.str());
          }
        }
      }
    }
  public:
    AliasingFromStructure(Section* section, std::vector<Structure*> inputs): Pass(section), _inputs(inputs) {
      apply(section);
    }

    AliasingFromStructure(Section* section, std::function<std::vector<Structure*>(StructureBuilder&)> fn): Pass(section) {
      StructureBuilder builder;
      _inputs = fn(builder);
      apply(section);
    } 
  };
}

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
  metajit::AliasingFromStructure::run(section, [](metajit::AliasingFromStructure::StructureBuilder& builder) -> std::vector<metajit::AliasingFromStructure::Structure*> {
    return {
      builder.record({
        builder.field(offsetof(Uxn, ram), builder.array()),
        builder.field(offsetof(Uxn, dev), builder.array()),
        builder.field(offsetof(Uxn, wst) + offsetof(Stack, dat), builder.array()),
        builder.field(offsetof(Uxn, wst) + offsetof(Stack, ptr), nullptr),
        builder.field(offsetof(Uxn, rst) + offsetof(Stack, dat), builder.array()),
        builder.field(offsetof(Uxn, rst) + offsetof(Stack, ptr), nullptr),
        builder.field(offsetof(Uxn, pc), nullptr)
      })
    };
  });
  metajit::Simplify::run(section, 5);
  metajit::DeadStoreElim::run(section);
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
  uxn.dev = new uint8_t[0x100]();
  uxn.rst.dat = new uint8_t[0x100]();
  uxn.wst.dat = new uint8_t[0x100]();
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
      uint8_t return_stack_top = uxn.rst.ptr;
      std::set<uint16_t> visited;
      for (size_t it = 0; it < 64; it++) {
        if (uxn.rst.ptr < return_stack_top) {
          break; // Stop tracing since, we could have returned to many different locations
        }
        if (visited.find(uxn.pc) != visited.end()) {
          break;
        }
        visited.insert(uxn.pc);

        {
          std::ostringstream stream;
          stream << "Step " << it << " at PC " << std::hex << uxn.pc << std::dec;
          trace_builder.build_comment(stream.str());
        }

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

      if (trace_section->verify(std::cerr)) {
        return 1;
      }

      metajit::DeadCodeElim::run(trace_section);
      metajit::DeadStoreElim::run(trace_section);
      metajit::DeadCodeElim::run(trace_section);
      
      trace_section->write(std::cerr);

      if (trace_section->verify(std::cerr)) {
        return 1;
      }

      exit(0);
    } else {
      std::cerr << "Step at PC " << std::hex << uxn.pc << " Opcode " << (uint64_t)uxn.ram[uxn.pc] << std::dec << "\n";
      std::cerr << "  Return Stack:";
      for (size_t i = 0; i < uxn.rst.ptr; i++) {
        std::cerr << " " << std::hex << (uint64_t)uxn.rst.dat[i] << std::dec;
      }
      std::cerr << "\n";

      step((uint8_t*) &uxn);
    }
  }

  return 0;
}