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

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

#include "wasp/base/features.h"
#include "wasp/base/file.h"
#include "wasp/base/formatters.h"
#include "wasp/base/macros.h"
#include "wasp/base/string_view.h"
#include "wasp/base/types.h"
#include "wasp/binary/data_count_section.h"
#include "wasp/binary/errors.h"
#include "wasp/binary/formatters.h"
#include "wasp/binary/lazy_code_section.h"
#include "wasp/binary/lazy_comdat_subsection.h"
#include "wasp/binary/lazy_data_section.h"
#include "wasp/binary/lazy_element_section.h"
#include "wasp/binary/lazy_export_section.h"
#include "wasp/binary/lazy_expression.h"
#include "wasp/binary/lazy_function_names_subsection.h"
#include "wasp/binary/lazy_function_section.h"
#include "wasp/binary/lazy_global_section.h"
#include "wasp/binary/lazy_import_section.h"
#include "wasp/binary/lazy_init_functions_subsection.h"
#include "wasp/binary/lazy_local_names_subsection.h"
#include "wasp/binary/lazy_memory_section.h"
#include "wasp/binary/lazy_module.h"
#include "wasp/binary/lazy_module_name_subsection.h"
#include "wasp/binary/lazy_name_section.h"
#include "wasp/binary/lazy_segment_info_subsection.h"
#include "wasp/binary/lazy_symbol_table_subsection.h"
#include "wasp/binary/lazy_table_section.h"
#include "wasp/binary/lazy_type_section.h"
#include "wasp/binary/linking_section.h"
#include "wasp/binary/relocation_section.h"
#include "wasp/binary/start_section.h"

namespace wasp {
namespace tools {
namespace dump {

using namespace ::wasp::binary;

enum class Pass {
  Headers,
  Details,
  Disassemble,
  RawData,
};

class ErrorsBasic : public Errors {
 public:
  explicit ErrorsBasic(SpanU8 data) : data{data} {}

 protected:
  void HandlePushContext(SpanU8 pos, string_view desc) override {}
  void HandlePopContext() override {}
  void HandleOnError(SpanU8 pos, string_view message) override {
    print("{:08x}: {}\n", pos.data() - data.data(), message);
  }

  SpanU8 data;
};

struct Options {
  Features features;
  bool print_headers = false;
  bool print_details = false;
  bool print_disassembly = false;
  bool print_raw_data = false;
  string_view section_name;
};

struct Tool {
  explicit Tool(string_view filename, SpanU8 data, Options);

  using SectionIndex = u32;

  void Run();
  void DoPrepass();
  void DoPass(Pass);
  bool SectionMatches(Section) const;
  void DoSectionHeader(Pass, Section);
  void DoKnownSection(Pass, SectionIndex, KnownSection);
  void DoCustomSection(Pass, SectionIndex, CustomSection);

  void DoTypeSection(Pass, SectionIndex, LazyTypeSection);
  void DoImportSection(Pass, SectionIndex, LazyImportSection);
  void DoFunctionSection(Pass, SectionIndex, LazyFunctionSection);
  void DoTableSection(Pass, SectionIndex, LazyTableSection);
  void DoMemorySection(Pass, SectionIndex, LazyMemorySection);
  void DoGlobalSection(Pass, SectionIndex, LazyGlobalSection);
  void DoExportSection(Pass, SectionIndex, LazyExportSection);
  void DoStartSection(Pass, SectionIndex, StartSection);
  void DoElementSection(Pass, SectionIndex, LazyElementSection);
  void DoCodeSection(Pass, SectionIndex, LazyCodeSection);
  void DoDataSection(Pass, SectionIndex, LazyDataSection);
  void DoDataCountSection(Pass, SectionIndex, DataCountSection);
  void DoNameSection(Pass, SectionIndex, LazyNameSection);
  void DoLinkingSection(Pass, SectionIndex, LinkingSection);
  void DoRelocationSection(Pass, SectionIndex, RelocationSection);

  void DoCount(Pass, optional<Index> count);

  void Disassemble(SectionIndex, Index func_index, Code);

  void InsertFunctionName(Index, string_view name);
  void InsertGlobalName(Index, string_view name);
  optional<FunctionType> GetFunctionType(Index) const;
  optional<string_view> GetFunctionName(Index) const;
  optional<string_view> GetGlobalName(Index) const;
  optional<string_view> GetSectionName(Index) const;
  optional<string_view> GetSymbolName(Index) const;
  optional<Index> GetI32Value(const ConstantExpression&);

  using RelocationEntries = std::vector<RelocationEntry>;
  optional<RelocationEntries> GetRelocationEntries(SectionIndex);

  enum class PrintChars { No, Yes };

  bool ShouldPrintDetails(Pass) const;
  template <typename... Args>
  void PrintDetails(Pass, const char* format, const Args&...);
  void PrintFunctionName(Index func_index);
  void PrintGlobalName(Index global_index);
  void PrintMemory(SpanU8 data,
                   Index offset,
                   PrintChars print_chars = PrintChars::Yes,
                   string_view prefix = "",
                   int octets_per_line = 16,
                   int octets_per_group = 2);

  size_t file_offset(SpanU8 data);

  struct Symbol {
    SymbolInfoKind kind;
    std::string name;
    Index index;
  };

  std::string filename;
  Options options;
  SpanU8 data;
  ErrorsBasic errors;
  LazyModule module;
  std::vector<TypeEntry> type_entries;
  std::vector<Function> functions;
  std::map<Index, string_view> function_names;
  std::map<Index, string_view> global_names;
  std::map<Index, Symbol> symbol_table;
  std::map<SectionIndex, std::string> section_names;
  std::map<SectionIndex, size_t> section_starts;
  std::map<SectionIndex, RelocationEntries> section_relocations;
  bool should_print_details = true;
  Index imported_function_count = 0;
  Index imported_table_count = 0;
  Index imported_memory_count = 0;
  Index imported_global_count = 0;
};

int Main(int argc, char** argv) {
  std::vector<string_view> filenames;
  Options options;
  options.features.EnableAll();

  for (int i = 0; i < argc; ++i) {
    string_view arg = argv[i];
    if (arg[0] == '-') {
      switch (arg[1]) {
        case 'h': options.print_headers = true; break;
        case 'd': options.print_disassembly = true; break;
        case 'x': options.print_details = true; break;
        case 's': options.print_raw_data = true; break;
        case 'j': options.section_name = argv[++i]; break;
        case '-':
          if (arg == "--headers") {
            options.print_headers = true;
          } else if (arg == "--disassemble") {
            options.print_disassembly = true;
          } else if (arg == "--details") {
            options.print_details = true;
          } else if (arg == "--full-contents") {
            options.print_raw_data = true;
          } else if (arg == "--section") {
            options.section_name = argv[++i];
          } else {
            print("Unknown long argument {}\n", arg);
          }
          break;
        default:
          print("Unknown short argument {}\n", arg[0]);
          break;
      }
    } else {
      filenames.push_back(arg);
    }
  }

  if (filenames.empty()) {
    print("No filenames given.\n");
    return 1;
  }

  if (!(options.print_headers || options.print_disassembly ||
        options.print_details || options.print_raw_data)) {
    print("At least one of the following switches must be given:\n");
    print(" -d/--disassemble\n");
    print(" -h/--headers\n");
    print(" -x/--details\n");
    print(" -s/--full-contents\n");
    return 1;
  }

  for (auto filename : filenames) {
    auto optbuf = ReadFile(filename);
    if (!optbuf) {
      print("Error reading file {}.\n", filename);
      continue;
    }

    SpanU8 data{*optbuf};
    Tool tool{filename, data, options};
    tool.Run();
  }

  return 0;
}

Tool::Tool(string_view filename, SpanU8 data, Options options)
    : filename(filename),
      options{options},
      data{data},
      errors{data},
      module{ReadModule(data, options.features, errors)} {}

void Tool::Run() {
  if (!(module.magic && module.version)) {
    return;
  }

  print("\n{}:\tfile format wasm {}\n", filename, *module.version);
  DoPrepass();
  if (options.print_headers) {
    DoPass(Pass::Headers);
  }
  if (options.print_details) {
    DoPass(Pass::Details);
  }
  if (options.print_disassembly) {
    DoPass(Pass::Disassemble);
  }
  if (options.print_raw_data) {
    DoPass(Pass::RawData);
  }
}

void Tool::DoPrepass() {
  const Features& features = options.features;
  SectionIndex section_index = 0;
  for (auto section : module.sections) {
    section_starts[section_index] = file_offset(section.data());
    if (section.is_known()) {
      auto known = section.known();
      section_names[section_index] = format("{}", known.id);
      switch (known.id) {
        case SectionId::Type: {
          auto seq = ReadTypeSection(known, features, errors).sequence;
          std::copy(seq.begin(), seq.end(), std::back_inserter(type_entries));
          break;
        }

        case SectionId::Import: {
          for (auto import :
               ReadImportSection(known, features, errors).sequence) {
            switch (import.kind()) {
              case ExternalKind::Function:
                functions.push_back(Function{import.index()});
                InsertFunctionName(imported_function_count++, import.name);
                break;

              case ExternalKind::Table:
                imported_table_count++;
                break;

              case ExternalKind::Memory:
                imported_memory_count++;
                break;

              case ExternalKind::Global:
                InsertGlobalName(imported_global_count++, import.name);
                break;

              default:
                break;
            }
          }
          break;
        }

        case SectionId::Function: {
          auto seq = ReadFunctionSection(known, features, errors).sequence;
          std::copy(seq.begin(), seq.end(), std::back_inserter(functions));
          break;
        }

        case SectionId::Export: {
          for (auto export_ :
               ReadExportSection(known, features, errors).sequence) {
            switch (export_.kind) {
              case ExternalKind::Function:
                InsertFunctionName(export_.index, export_.name);
                break;

              case ExternalKind::Global:
                InsertGlobalName(export_.index, export_.name);
                break;

              default:
                break;
            }
          }
        }

        default:
          break;
      }
    } else if (section.is_custom()) {
      auto custom = section.custom();
      section_names[section_index] = custom.name.to_string();
      if (custom.name == "name") {
        for (auto subsection : ReadNameSection(custom, features, errors)) {
          if (subsection.id == NameSubsectionId::FunctionNames) {
            for (auto name_assoc :
                 ReadFunctionNamesSubsection(subsection, features, errors)
                     .sequence) {
              InsertFunctionName(name_assoc.index, name_assoc.name);
            }
          }
        }
      } else if (custom.name == "linking") {
        for (auto subsection :
             ReadLinkingSection(custom, features, errors).subsections) {
          if (subsection.id == LinkingSubsectionId::SymbolTable) {
            Index symbol_index = 0;
            for (auto symbol :
                 ReadSymbolTableSubsection(subsection, features, errors)
                     .sequence) {
              auto kind = symbol.kind();
              auto name_opt = symbol.name();
              auto name = name_opt.value_or("").to_string();
              if (symbol.is_base()) {
                auto item_index = symbol.base().index;
                if (name_opt) {
                  if (kind == SymbolInfoKind::Function) {
                    InsertFunctionName(item_index, *name_opt);
                  } else if (kind == SymbolInfoKind::Global) {
                    InsertGlobalName(item_index, *name_opt);
                  }
                }
                symbol_table[symbol_index] = Symbol{kind, name, item_index};
              } else if (symbol.is_data()) {
                symbol_table[symbol_index] = Symbol{kind, name, 0};
              } else if (symbol.is_section()) {
                symbol_table[symbol_index] =
                    Symbol{kind, name, symbol.section().section};
              }
              ++symbol_index;
            }
          }
        }
      } else if (custom.name.starts_with("reloc.")) {
        auto sec = ReadRelocationSection(custom, features, errors);
        if (sec.section_index) {
          section_relocations[*sec.section_index] =
              RelocationEntries{sec.entries.begin(), sec.entries.end()};
        }
      }
    }
    ++section_index;
  }
}

void Tool::DoPass(Pass pass) {
  switch (pass) {
    case Pass::Headers:
      print("\nSections:\n\n");
      break;

    case Pass::Details:
      print("\nSection Details:\n\n");
      break;

    case Pass::Disassemble:
      print("\nCode Disassembly:\n\n");
      break;

    case Pass::RawData:
      break;
  }

  SectionIndex section_index = 0;
  for (auto section : module.sections) {
    if (SectionMatches(section)) {
      DoSectionHeader(pass, section);
      if (section.is_known()) {
        DoKnownSection(pass, section_index, section.known());
      } else if (section.is_custom()) {
        DoCustomSection(pass, section_index, section.custom());
      }
    }
    ++section_index;
  }
}

namespace {

template <typename S1, typename S2>
bool StringsAreEqualCaseInsensitive(const S1& s1, const S2& s2) {
  auto it1 = s1.begin(), end1 = s1.end();
  auto it2 = s2.begin(), end2 = s2.end();
  for (; it1 != end1 && it2 != end2; ++it1, ++it2) {
    if (std::tolower(*it1) != std::tolower(*it2)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool Tool::SectionMatches(Section section) const {
  if (options.section_name.empty()) {
    return true;
  }

  std::string name;
  if (section.is_known()) {
    name = format("{}", section.known().id);
  } else if (section.is_custom()) {
    name = section.custom().name.to_string();
  }
  return StringsAreEqualCaseInsensitive(name, options.section_name);
}

void Tool::DoKnownSection(Pass pass,
                          SectionIndex section_index,
                          KnownSection known) {
  const Features& features = options.features;
  switch (known.id) {
    case SectionId::Custom:
      WASP_UNREACHABLE();
      break;

    case SectionId::Type:
      DoTypeSection(pass, section_index,
                    ReadTypeSection(known, features, errors));
      break;

    case SectionId::Import:
      DoImportSection(pass, section_index,
                      ReadImportSection(known, features, errors));
      break;

    case SectionId::Function:
      DoFunctionSection(pass, section_index,
                        ReadFunctionSection(known, features, errors));
      break;

    case SectionId::Table:
      DoTableSection(pass, section_index,
                     ReadTableSection(known, features, errors));
      break;

    case SectionId::Memory:
      DoMemorySection(pass, section_index,
                      ReadMemorySection(known, features, errors));
      break;

    case SectionId::Global:
      DoGlobalSection(pass, section_index,
                      ReadGlobalSection(known, features, errors));
      break;

    case SectionId::Export:
      DoExportSection(pass, section_index,
                      ReadExportSection(known, features, errors));
      break;

    case SectionId::Start:
      DoStartSection(pass, section_index,
                     ReadStartSection(known, features, errors));
      break;

    case SectionId::Element:
      DoElementSection(pass, section_index,
                       ReadElementSection(known, features, errors));
      break;

    case SectionId::Code:
      DoCodeSection(pass, section_index,
                    ReadCodeSection(known, features, errors));
      break;

    case SectionId::Data:
      DoDataSection(pass, section_index,
                    ReadDataSection(known, features, errors));
      break;

    case SectionId::DataCount:
      DoDataCountSection(pass, section_index,
                         ReadDataCountSection(known, features, errors));
      break;
  }
}

void Tool::DoCustomSection(Pass pass,
                           SectionIndex section_index,
                           CustomSection custom) {
  const Features& features = options.features;
  switch (pass) {
    case Pass::Headers:
      print("\"{}\"\n", custom.name);
      break;

    case Pass::Details:
      print(":\n - name: \"{}\"\n", custom.name);
      if (custom.name == "name") {
        DoNameSection(pass, section_index,
                      ReadNameSection(custom, features, errors));
      } else if (custom.name == "linking") {
        DoLinkingSection(pass, section_index,
                         ReadLinkingSection(custom, features, errors));
      } else if (custom.name.starts_with("reloc.")) {
        DoRelocationSection(pass, section_index,
                            ReadRelocationSection(custom, features, errors));
      }
      break;

    default:
      break;
  }
}

void Tool::DoSectionHeader(Pass pass,
                           Section section) {
  auto id = section.is_known() ? section.known().id : SectionId::Custom;
  auto data = section.data();
  auto offset = data.begin() - module.data.begin();
  auto size = data.size();
  switch (pass) {
    case Pass::Headers: {
      print("{:>9} start={:#010x} end={:#010x} (size={:#010x}) ", id, offset,
            offset + size, size);
      break;
    }

    case Pass::Details:
      print("{}", id);
      break;

    case Pass::Disassemble:
      break;

    case Pass::RawData: {
      if (section.is_custom()) {
        print("\nContents of custom section ({}):\n", section.custom().name);
      } else {
        print("\nContents of section {}:\n", id);
      }
      PrintMemory(data, offset, PrintChars::Yes);
      break;
    }
  }
}

void Tool::DoTypeSection(Pass pass,
                         SectionIndex section_index,
                         LazyTypeSection section) {
  DoCount(pass, section.count);
  if (ShouldPrintDetails(pass)) {
    Index count = 0;
    for (auto type_entry : section.sequence) {
      print(" - type[{}] {}\n", count++, type_entry);
    }
  }
}

void Tool::DoImportSection(Pass pass,
                           SectionIndex section_index,
                           LazyImportSection section) {
  DoCount(pass, section.count);
  if (ShouldPrintDetails(pass)) {
    Index function_count = 0;
    Index table_count = 0;
    Index memory_count = 0;
    Index global_count = 0;
    for (auto import : section.sequence) {
      switch (import.kind()) {
        case ExternalKind::Function: {
          print(" - func[{}] sig={}", function_count, import.index());
          PrintFunctionName(function_count);
          ++function_count;
          break;
        }

        case ExternalKind::Table: {
          print(" - table[{}] {}", table_count, import.table_type());
          ++table_count;
          break;
        }

        case ExternalKind::Memory: {
          print(" - memory[{}] {}", memory_count, import.memory_type());
          ++memory_count;
          break;
        }

        case ExternalKind::Global: {
          print(" - global[{}] {}", global_count, import.global_type());
          ++global_count;
          break;
        }
      }
      print(" <- {}.{}\n", import.module, import.name);
    }
  }
}

void Tool::DoFunctionSection(Pass pass,
                             SectionIndex section_index,
                             LazyFunctionSection section) {
  DoCount(pass, section.count);
  if (ShouldPrintDetails(pass)) {
    Index count = imported_function_count;
    for (auto func : section.sequence) {
      print(" - func[{}] sig={}", count, func.type_index);
      PrintFunctionName(count);
      print("\n");
      ++count;
    }
  }
}

void Tool::DoTableSection(Pass pass,
                          SectionIndex section_index,
                          LazyTableSection section) {
  DoCount(pass, section.count);
  if (ShouldPrintDetails(pass)) {
    Index count = imported_table_count;
    for (auto table : section.sequence) {
      print(" - table[{}] {}\n", count, table.table_type);
      ++count;
    }
  }
}

void Tool::DoMemorySection(Pass pass,
                           SectionIndex section_index,
                           LazyMemorySection section) {
  DoCount(pass, section.count);
  if (ShouldPrintDetails(pass)) {
    Index count = imported_memory_count;
    for (auto memory : section.sequence) {
      print(" - memory[{}] {}\n", count, memory.memory_type);
      ++count;
    }
  }
}

void Tool::DoGlobalSection(Pass pass,
                           SectionIndex section_index,
                           LazyGlobalSection section) {
  DoCount(pass, section.count);
  if (ShouldPrintDetails(pass)) {
    Index count = imported_global_count;
    for (auto global : section.sequence) {
      print(" - global[{}] {} - {}\n", count, global.global_type, global.init);
      ++count;
    }
  }
}

void Tool::DoExportSection(Pass pass,
                           SectionIndex section_index,
                           LazyExportSection section) {
  DoCount(pass, section.count);
  if (ShouldPrintDetails(pass)) {
    Index count = 0;
    for (auto export_ : section.sequence) {
      print(" - {}[{}]", export_.kind, export_.index);
      if (export_.kind == ExternalKind::Function) {
        PrintFunctionName(export_.index);
      }
      print(" -> \"{}\"\n", export_.name);
      ++count;
    }
  }
}

void Tool::DoStartSection(Pass pass,
                          SectionIndex section_index,
                          StartSection section) {
  if (section) {
    auto start = *section;
    if (pass == Pass::Headers) {
      print("start: {}\n", start.func_index);
    } else {
      PrintDetails(pass, " - start function: {}\n", start.func_index);
    }
  }
}

void Tool::DoElementSection(Pass pass,
                            SectionIndex section_index,
                            LazyElementSection section) {
  DoCount(pass, section.count);
  if (ShouldPrintDetails(pass)) {
    Index count = 0;
    for (auto element : section.sequence) {
      Index offset = 0;
      if (element.is_active()) {
        const auto& active = element.active();
        print(" - segment[{}] table={} count={} - init {}\n", count,
              active.table_index, active.init.size(), active.offset);
        offset = GetI32Value(element.active().offset).value_or(0);
        Index elem_count = 0;
        for (auto func_index : active.init) {
          print("  - elem[{}] = func[{}]", offset + elem_count, func_index);
          PrintFunctionName(func_index);
          print("\n");
          ++elem_count;
        }
      } else {
        const auto& passive = element.passive();
        print(" - segment[{}] count={} element_type={} passive\n", count,
              passive.element_type, passive.init.size());
        Index elem_count = 0;
        for (auto element : passive.init) {
          print("  - elem[{}] = {}\n", offset + elem_count, element);
          ++elem_count;
        }
      }

      ++count;
    }
  }
}

void Tool::DoCodeSection(Pass pass,
                         SectionIndex section_index,
                         LazyCodeSection section) {
  DoCount(pass, section.count);
  if (ShouldPrintDetails(pass)) {
    Index count = imported_function_count;
    for (auto code : section.sequence) {
      print(" - func[{}] size={}\n", count, code.body.data.size());
      ++count;
    }
  } else if (pass == Pass::Disassemble) {
    Index count = imported_function_count;
    for (auto code : section.sequence) {
      Disassemble(section_index, count, code);
      ++count;
    }
  }
}

void Tool::DoDataSection(Pass pass,
                         SectionIndex section_index,
                         LazyDataSection section) {
  DoCount(pass, section.count);
  if (ShouldPrintDetails(pass)) {
    Index count = 0;
    for (auto data : section.sequence) {
      Index offset = 0;
      if (data.is_active()) {
        print(" - segment[{}] memory={} size={} - init {}\n", count,
              data.active().memory_index, data.init.size(),
              data.active().offset);
        offset = GetI32Value(data.active().offset).value_or(0);
      } else {
        print(" - segment[{}] size={} passive\n", count, data.init.size());
      }
      PrintMemory(data.init, offset, PrintChars::Yes, "  - ");
      ++count;
    }
  }
}

void Tool::DoDataCountSection(Pass pass,
                              SectionIndex section_index,
                              DataCountSection section) {
  if (section) {
    auto data_count = *section;
    if (pass == Pass::Headers) {
      print("count: {}\n", data_count.count);
    } else {
      PrintDetails(pass, " - data count: {}\n", data_count.count);
    }
  }
}

void Tool::DoNameSection(Pass pass,
                         SectionIndex section_index,
                         LazyNameSection section) {
  const Features& features = options.features;
  for (auto subsection : section) {
    switch (subsection.id) {
      case NameSubsectionId::ModuleName: {
        auto module_name =
            ReadModuleNameSubsection(subsection.data, features, errors);
        print("  module name: {}\n", module_name.value_or(""));
        break;
      }

      case NameSubsectionId::FunctionNames: {
        auto function_names_subsection =
            ReadFunctionNamesSubsection(subsection.data, features, errors);
        print("  function names[{}]:\n",
              function_names_subsection.count.value_or(0));
        Index count = 0;
        for (auto name_assoc : function_names_subsection.sequence) {
          print("   - [{}]: func[{}] name=\"{}\"\n", count++, name_assoc.index,
                name_assoc.name);
        }
        break;
      }

      case NameSubsectionId::LocalNames: {
        auto local_names_subsection =
            ReadLocalNamesSubsection(subsection.data, features, errors);
        print("  local names[{}]:\n", local_names_subsection.count.value_or(0));
        Index func_count = 0;
        for (auto indirect_name_assoc : local_names_subsection.sequence) {
          print("   - [{}]: func[{}] count={}\n", func_count++,
                indirect_name_assoc.index, indirect_name_assoc.name_map.size());
          Index local_count = 0;
          for (auto name_assoc : indirect_name_assoc.name_map) {
            print("     - [{}]: local[{}] name=\"{}\"\n", local_count++,
                  name_assoc.index, name_assoc.name);
          }
        }
        break;
      }
    }
  }
}

void Tool::DoLinkingSection(Pass pass,
                            SectionIndex section_index,
                            LinkingSection section) {
  const Features& features = options.features;
  for (auto subsection : section.subsections) {
    switch (subsection.id) {
      case LinkingSubsectionId::SegmentInfo: {
        if (ShouldPrintDetails(pass)) {
          auto segment_infos =
              ReadSegmentInfoSubsection(subsection.data, features, errors);
          print(" - segment info [count={}]\n",
                segment_infos.count.value_or(0));
          Index index = 0;
          for (auto segment_info : segment_infos.sequence) {
            print("  - {}: {} p2align={} flags={:#x}\n", index,
                  segment_info.name, segment_info.align_log2,
                  segment_info.flags);
            ++index;
          }
        }
        break;
      }

      case LinkingSubsectionId::InitFunctions: {
        if (ShouldPrintDetails(pass)) {
          auto init_functions =
              ReadInitFunctionsSubsection(subsection.data, features, errors);
          print(" - init functions [count={}]\n",
                init_functions.count.value_or(0));
          for (auto init_function : init_functions.sequence) {
            print("  - {}: priority={}\n", init_function.index,
                  init_function.priority);
          }
        }
        break;
      }

      case LinkingSubsectionId::ComdatInfo: {
        if (ShouldPrintDetails(pass)) {
          auto comdats =
              ReadComdatSubsection(subsection.data, features, errors);
          print(" - comdat [count={}]\n", comdats.count.value_or(0));
          Index comdat_index = 0;
          for (auto comdat : comdats.sequence) {
            print("  - {}: \"{}\" flags={:#x} [count={}]\n",
                  comdat_index, comdat.name, comdat.flags);
            Index symbol_index = 0;
            for (auto symbol : comdat.symbols) {
              print("   - {}: {} index={}\n", symbol_index, symbol.kind,
                    symbol.index);
              ++symbol_index;
            }
            ++comdat_index;
          }
        }
        break;
      }

      case LinkingSubsectionId::SymbolTable: {
        if (ShouldPrintDetails(pass)) {
          auto print_symbol_flags = [&](SymbolInfo::Flags flags) {
            if (flags.undefined == SymbolInfo::Flags::Undefined::Yes) {
              print(" {}", flags.undefined);
            }
            print(" binding={} vis={}", flags.binding, flags.visibility);
            if (flags.explicit_name == SymbolInfo::Flags::ExplicitName::Yes) {
              print(" {}", flags.explicit_name);
            }
          };

          auto symbol_table =
              ReadSymbolTableSubsection(subsection.data, features, errors);
          print(" - symbol table [count={}]\n",
                symbol_table.count.value_or(0));
          Index index = 0;
          for (auto symbol : symbol_table.sequence) {
            switch (symbol.kind()) {
              case SymbolInfoKind::Function: {
                const auto& base = symbol.base();
                print("  - {}: F <{}> func={}", index,
                      base.name.value_or(
                          GetFunctionName(base.index).value_or("")),
                      base.index);
                print_symbol_flags(symbol.flags);
                break;
              }

              case SymbolInfoKind::Global: {
                const auto& base = symbol.base();
                print(
                    "  - {}: G <{}> global={}", index,
                    base.name.value_or(GetGlobalName(base.index).value_or("")),
                    base.index);
                print_symbol_flags(symbol.flags);
                break;
              }

              case SymbolInfoKind::Event: {
                const auto& base = symbol.base();
                // TODO GetEventName.
                print("  - {}: E <{}> event={}", index, base.name.value_or(""),
                      base.index);
                print_symbol_flags(symbol.flags);
                break;
              }

              case SymbolInfoKind::Data: {
                const auto& data = symbol.data();
                print("  - {}: D <{}>", index, data.name);
                if (data.defined) {
                  print(" segment={} offset={} size={}", data.defined->index,
                        data.defined->offset, data.defined->size);
                }
                print_symbol_flags(symbol.flags);
                break;
              }

              case SymbolInfoKind::Section: {
                auto section_index = symbol.section().section;
                print("  - {}: S <{}> section={}", index,
                      GetSectionName(section_index).value_or(""),
                      section_index);
                print_symbol_flags(symbol.flags);
                break;
              }
            }
            print("\n");
            ++index;
          }
        }
        break;
      }
    }
  }
}

void Tool::DoRelocationSection(Pass pass,
                               SectionIndex section_index,
                               RelocationSection section) {
  const Features& features = options.features;
  auto reloc_section_index = section.section_index.value_or(-1);
  PrintDetails(pass, " - relocations for section {} ({}) [{}]\n",
               reloc_section_index,
               GetSectionName(reloc_section_index).value_or(""),
               section.count.value_or(0));
  for (auto entry : section.entries) {
    size_t total_offset = entry.offset;
    auto start = section_starts.find(*section.section_index);
    if (start != section_starts.end()) {
      total_offset += start->second;
    }
    if (ShouldPrintDetails(pass)) {
      print("   - {:18s} offset={:#08x}(file={:#08x}) ", entry.type,
            entry.offset, total_offset);
      if (entry.type == RelocationType::TypeIndexLEB) {
        print("type={}", entry.index);
      } else {
        print("symbol={} <{}>", entry.index,
              GetSymbolName(entry.index).value_or(""));
      }
      if (entry.addend && *entry.addend != 0) {
        print("{:+#x}", *entry.addend);
      }
      print("\n");
    }
  }
}

void Tool::DoCount(Pass pass, optional<Index> count) {
  if (pass == Pass::Headers) {
    print("count: {}\n", count.value_or(0));
  } else {
    PrintDetails(pass, "[{}]:\n", count.value_or(0));
  }
}

void Tool::Disassemble(SectionIndex section_index,
                       Index func_index,
                       Code code) {
  constexpr int max_octets_per_line = 9;
  auto func_type = GetFunctionType(func_index);
  int param_count = 0;
  print("func[{}]", func_index);
  PrintFunctionName(func_index);
  print(":");
  if (func_type) {
    print(" {}\n", *func_type);
    param_count = func_type->param_types.size();
  } else {
    print("\n");
  }
  int local_count = param_count;
  for (auto locals : code.locals) {
    print(" {:{}s} | locals[{}", "", 7 + max_octets_per_line * 3, local_count);
    if (locals.count != 1) {
      print("..{}", local_count + locals.count - 1);
    }
    print("] type={}\n", locals.type);
    local_count += locals.count;
  }
  int indent = 0;
  auto section_start = section_starts[section_index];
  auto last_data = code.body.data;
  auto relocs =
      GetRelocationEntries(section_index).value_or(RelocationEntries{});
  auto reloc_it = relocs.begin();
  auto instrs = ReadExpression(code.body, options.features, errors);
  for (auto it = instrs.begin(), end = instrs.end(); it != end; ++it) {
    auto instr = *it;
    if (instr.opcode == Opcode::Else || instr.opcode == Opcode::End) {
      indent = std::max(indent - 2, 0);
    }
    bool first_line = true;
    while (last_data.begin() < it.data().begin()) {
      print(" {:06x}:", file_offset(last_data));
      int line_octets = std::min<int>(max_octets_per_line,
                                      it.data().begin() - last_data.begin());
      for (int i = 0; i < line_octets; ++i) {
        print(" {:02x}", last_data[i]);
      }
      remove_prefix(&last_data, line_octets);
      print("{:{}s} |", "", (max_octets_per_line - line_octets) * 3);
      if (first_line) {
        first_line = false;
        print(" {:{}s}{}", "", indent, instr);
        if (instr.opcode == Opcode::Call) {
          PrintFunctionName(instr.index_immediate());
        } else if (instr.opcode == Opcode::GlobalGet ||
                   instr.opcode == Opcode::GlobalSet) {
          PrintGlobalName(instr.index_immediate());
        }
      }
      print("\n");
    }
    if (reloc_it < relocs.end()) {
      auto offset = section_start + reloc_it->offset;
      if (offset < file_offset(it.data())) {
        print("           {:06x}: {:18s} {}", offset, reloc_it->type,
              reloc_it->index);
        if (reloc_it->addend && *reloc_it->addend) {
          print(" {:+d}", *reloc_it->addend);
        }
        if (reloc_it->type != RelocationType::TypeIndexLEB) {
          print(" <{}>", GetSymbolName(reloc_it->index).value_or(""));
        }
        print("\n");
        ++reloc_it;
      }
    }
    if (instr.opcode == Opcode::Block || instr.opcode == Opcode::If ||
        instr.opcode == Opcode::Loop || instr.opcode == Opcode::Else) {
      indent += 2;
    }
  }
}

void Tool::InsertFunctionName(Index index, string_view name) {
  function_names.insert(std::make_pair(index, name));
}

void Tool::InsertGlobalName(Index index, string_view name) {
  global_names.insert(std::make_pair(index, name));
}

optional<FunctionType> Tool::GetFunctionType(Index func_index) const {
  if (func_index >= functions.size() ||
      functions[func_index].type_index >= type_entries.size()) {
    return nullopt;
  }
  return type_entries[functions[func_index].type_index].type;
}

optional<string_view> Tool::GetFunctionName(Index index) const {
  auto it = function_names.find(index);
  if (it != function_names.end()) {
    return it->second;
  } else {
    return nullopt;
  }
}

optional<string_view> Tool::GetGlobalName(Index index) const {
  auto it = global_names.find(index);
  if (it != global_names.end()) {
    return it->second;
  } else {
    return nullopt;
  }
}

optional<string_view> Tool::GetSectionName(Index index) const {
  auto it = section_names.find(index);
  if (it != section_names.end()) {
    return it->second;
  } else {
    return nullopt;
  }
}

optional<string_view> Tool::GetSymbolName(Index index) const {
  auto it = symbol_table.find(index);
  if (it != symbol_table.end()) {
    const auto& symbol = it->second;
    switch (symbol.kind) {
      case SymbolInfoKind::Function:
        return GetFunctionName(symbol.index);

      case SymbolInfoKind::Data:
        return symbol.name;

      case SymbolInfoKind::Global:
        return GetGlobalName(symbol.index);

      case SymbolInfoKind::Section:
        return GetSectionName(symbol.index);

      case SymbolInfoKind::Event:
        return "";  // XXX
    }
  } else {
    return nullopt;
  }
}

optional<Index> Tool::GetI32Value(const ConstantExpression& expr) {
  switch (expr.instruction.opcode) {
    case Opcode::I32Const:
      return expr.instruction.s32_immediate();

    default:
      return nullopt;
  }
}

optional<Tool::RelocationEntries> Tool::GetRelocationEntries(
    SectionIndex section_index) {
  auto it = section_relocations.find(section_index);
  if (it != section_relocations.end()) {
    return it->second;
  } else {
    return nullopt;
  }
}

bool Tool::ShouldPrintDetails(Pass pass) const {
  return pass == Pass::Details && should_print_details;
}

template <typename... Args>
void Tool::PrintDetails(Pass pass, const char* format, const Args&... args) {
  if (ShouldPrintDetails(pass)) {
    vprint(format, make_format_args(args...));
  }
}

void Tool::PrintFunctionName(Index func_index) {
  if (auto name = GetFunctionName(func_index)) {
    print(" <{}>", *name);
  }
}

void Tool::PrintGlobalName(Index func_index) {
  if (auto name = GetGlobalName(func_index)) {
    print(" <{}>", *name);
  }
}

void Tool::PrintMemory(SpanU8 start,
                       Index offset,
                       PrintChars print_chars,
                       string_view prefix,
                       int octets_per_line,
                       int octets_per_group) {
  SpanU8 data = start;
  while (!data.empty()) {
    auto line_size = std::min<size_t>(data.size(), octets_per_line);
    const SpanU8 line = data.subspan(0, line_size);
    print("{}", prefix);
    print("{:07x}: ", (line.begin() - start.begin()) + offset);
    for (int i = 0; i < octets_per_line;) {
      for (int j = 0; j < octets_per_group; ++j, ++i) {
        if (i < line.size()) {
          print("{:02x}", line[i]);
        } else {
          print("  ");
        }
      }
      print(" ");
    }

    if (print_chars == PrintChars::Yes) {
      print(" ");
      for (int c : line) {
        print("{:c}", isprint(c) ? static_cast<char>(c) : '.');
      }
    }
    print("\n");
    remove_prefix(&data, line_size);
  }
}

size_t Tool::file_offset(SpanU8 data) {
  return data.begin() - module.data.begin();
}

}  // namespace dump
}  // namespace tools
}  // namespace wasp
