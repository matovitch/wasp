//
// Copyright 2018 WebAssembly Community Group participants
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
//

#include "wasp/binary/formatters.h"

#include <cassert>
#include <iostream>

namespace wasp {
namespace binary {

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::HeapType& self) {
  if (self.is_heap_kind()) {
    os << self.heap_kind();
  } else {
    assert(self.is_index());
    os << self.index();
  }
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::RefType& self) {
  os << "ref ";
  if (self.null == ::wasp::Null::Yes) {
    os << "null ";
  }
  os << self.heap_type;
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::ReferenceType& self) {
  if (self.is_reference_kind()) {
    os << self.reference_kind();
  } else {
    assert(self.is_ref());
    os << self.ref();
  }
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::ValueType& self) {
  if (self.is_numeric_type()) {
    os << self.numeric_type();
  } else {
    assert(self.is_reference_type());
    os << self.reference_type();
  }
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::VoidType& self) {
  return os << "void";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::BlockType& self) {
  if (self.is_value_type()) {
    os << "[" << self.value_type() << "]";
  } else if (self.is_void()) {
    os << "[]";
  } else {
    assert(self.is_index());
    os << "type[" << self.index() << "]";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::SectionId& self) {
  string_view result;
  switch (self) {
#define WASP_V(val, Name, str, ...)     \
  case ::wasp::binary::SectionId::Name: \
    result = str;                       \
    break;
#define WASP_FEATURE_V(...) WASP_V(__VA_ARGS__)
#include "wasp/binary/def/section_id.def"
#undef WASP_V
#undef WASP_FEATURE_V
    default: {
      // Special case for sections with unknown ids.
      return os << static_cast<::wasp::u32>(self);
    }
  }
  return os << result;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::LetImmediate& self) {
  return os << "{type " << self.block_type << ", locals " << self.locals << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::MemArgImmediate& self) {
  return os << "{align " << self.align_log2 << ", offset " << self.offset
            << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::Locals& self) {
  return os << self.type << " ** " << self.count;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::Section& self) {
  if (self.is_known()) {
    os << self.known();
  } else if (self.is_custom()) {
    os << self.custom();
  } else {
    WASP_UNREACHABLE();
  }
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::KnownSection& self) {
  return os << "{id " << self.id << ", contents " << self.data << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::CustomSection& self) {
  return os << "{name \"" << self.name << "\", contents " << self.data << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::TypeEntry& self) {
  return os << self.type;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::FunctionType& self) {
  return os << self.param_types << " -> " << self.result_types;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::TableType& self) {
  return os << self.limits << " " << self.elemtype;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::GlobalType& self) {
  return os << self.mut << " " << self.valtype;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::EventType& self) {
  return os << self.attribute << " " << self.type_index;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::Import& self) {
  os << "{module \"" << self.module << "\", name \"" << self.name << "\", desc "
     << self.kind() << " ";

  switch (self.kind()) {
    case ::wasp::ExternalKind::Function:
      os << self.index();
      break;

    case ::wasp::ExternalKind::Table:
      os << self.table_type();
      break;

    case ::wasp::ExternalKind::Memory:
      os << self.memory_type();
      break;

    case ::wasp::ExternalKind::Global:
      os << self.global_type();
      break;

    case ::wasp::ExternalKind::Event:
      os << self.event_type();
      break;

    default:
      WASP_UNREACHABLE();
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::Export& self) {
  return os << "{name \"" << self.name << "\", desc " << self.kind << " "
            << self.index << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::Expression& self) {
  return os << self.data;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::ConstantExpression& self) {
  return os << self.instructions << " end";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::ElementExpression& self) {
  return os << self.instructions << " end";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::CallIndirectImmediate& self) {
  return os << self.index << " " << self.table_index;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::BrTableImmediate& self) {
  return os << self.targets << " " << self.default_target;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::BrOnExnImmediate& self) {
  return os << self.target << " " << self.event_index;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::InitImmediate& self) {
  return os << self.segment_index << " " << self.dst_index;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::CopyImmediate& self) {
  return os << self.dst_index << " " << self.src_index;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::Instruction& self) {
  os << self.opcode;

  switch (self.immediate.index()) {
    case 0: /* Nothing. */ break;
    case 1: os << " " << self.s32_immediate(); break;
    case 2: os << " " << self.s64_immediate(); break;
    case 3: os << " " << self.f32_immediate(); break;
    case 4: os << " " << self.f64_immediate(); break;
    case 5: os << " " << self.v128_immediate(); break;
    case 6: os << " " << self.index_immediate(); break;
    case 7: os << " " << self.block_type_immediate(); break;
    case 8: os << " " << self.br_on_exn_immediate(); break;
    case 9: os << " " << self.br_table_immediate(); break;
    case 10: os << " " << self.call_indirect_immediate(); break;
    case 11: os << " " << self.copy_immediate(); break;
    case 12: os << " " << self.init_immediate(); break;
    case 13: os << " " << self.let_immediate(); break;
    case 14: os << " " << self.mem_arg_immediate(); break;
    case 15: os << " " << self.heap_type_immediate(); break;
    case 16: os << " " << self.select_immediate(); break;
    case 17: os << " " << self.shuffle_immediate(); break;
    case 18: os << " " << self.simd_lane_immediate(); break;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::InstructionList& self) {
  string_view space = "";
  for (const auto& x : self) {
    os << space << x;
    space = " ";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::Function& self) {
  return os << "{type " << self.type_index << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::Table& self) {
  return os << "{type " << self.table_type << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::Memory& self) {
  return os << "{type " << self.memory_type << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::Global& self) {
  return os << "{type " << self.global_type << ", init " << self.init << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::Start& self) {
  return os << "{func " << self.func_index << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::ElementSegment& self) {
  os << "{type ";
  if (self.has_indexes()) {
    os << self.indexes().kind << ", init " << self.indexes().list;
  } else if (self.has_expressions()) {
    os << self.expressions().elemtype << ", init " << self.expressions().list;
  }

  os << ", mode ";

  switch (self.type) {
    case ::wasp::SegmentType::Active:
      os << "active {table " << *self.table_index << ", offset " << *self.offset
         << "}";
      break;

    case ::wasp::SegmentType::Passive:
      os << "passive";
      break;

    case ::wasp::SegmentType::Declared:
      os << "declared";
      break;
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::ElementListWithIndexes& self) {
  return os << "{type " << self.kind << ", list " << self.list << "}";
}

std::ostream& operator<<(
    std::ostream& os,
    const ::wasp::binary::ElementListWithExpressions& self) {
  return os << "{type " << self.elemtype << ", init " << self.list << "}";
}

std::ostream& operator<<(std::ostream& os, const ::wasp::binary::Code& self) {
  return os << "{locals " << self.locals << ", body " << self.body << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::DataSegment& self) {
  os << "{init " << self.init << ", mode ";
  switch (self.type) {
    case ::wasp::SegmentType::Active:
      os << "active {memory " << *self.memory_index << ", offset "
         << *self.offset << "}";
      break;

    case ::wasp::SegmentType::Passive:
      os << "passive";
      break;

    case ::wasp::SegmentType::Declared:
      WASP_UNREACHABLE();
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::DataCount& self) {
  return os << "{count " << self.count << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::Event& self) {
  return os << "{type " << self.event_type << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::UnpackedCode& self) {
  return os << "{locals " << self.locals << ", body " << self.body << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::UnpackedExpression& self) {
  return os << self.instructions;
}

std::ostream& operator<<(std::ostream& os,
                         const ::wasp::binary::Module& self) {
  os << "{\n";
  os << "  types: " << self.types << "\n";
  os << "  imports: " << self.imports << "\n";
  os << "  functions: " << self.functions << "\n";
  os << "  tables: " << self.tables << "\n";
  os << "  memories: " << self.memories << "\n";
  os << "  globals: " << self.globals << "\n";
  os << "  events: " << self.events << "\n";
  os << "  exports: " << self.exports << "\n";
  os << "  start: " << self.start << "\n";
  os << "  element_segments: " << self.element_segments << "\n";
  os << "  data_count: " << self.data_count << "\n";
  os << "  codes: " << self.codes << "\n";
  os << "  data_segments: " << self.data_segments << "\n";
  os << "}\n";
  return os;
}

}  // namespace binary
}  // namespace wasp
