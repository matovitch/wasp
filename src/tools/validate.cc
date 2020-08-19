//
// Copyright 2019 WebAssembly Community Group participants
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

#include "wasp/valid/validate.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "fmt/format.h"
#include "fmt/ostream.h"

#include "src/tools/argparser.h"
#include "src/tools/binary_errors.h"
#include "wasp/base/enumerate.h"
#include "wasp/base/features.h"
#include "wasp/base/file.h"
#include "wasp/base/format.h"
#include "wasp/base/formatters.h"
#include "wasp/base/optional.h"
#include "wasp/base/string_view.h"
#include "wasp/binary/formatters.h"
#include "wasp/valid/context.h"
#include "wasp/valid/validate_visitor.h"

namespace wasp {
namespace tools {
namespace validate {

using fmt::print;

using namespace ::wasp::binary;

struct Options {
  Features features;
  bool verbose = false;
};

struct Tool {
  explicit Tool(string_view filename, SpanU8 data, Options);

  bool Run();

  std::string filename;
  Options options;
  SpanU8 data;
  BinaryErrors errors;
  LazyModule module;
  valid::ValidateVisitor visitor;
};

int Main(span<string_view> args) {
  std::vector<string_view> filenames;
  Options options;

  ArgParser parser{"wasp validate"};
  parser
      .Add('h', "--help", "print help and exit",
           [&]() { parser.PrintHelpAndExit(0); })
      .Add('v', "--verbose", "print filename and whether it was valid",
           [&]() { options.verbose = true; })
      .AddFeatureFlags(options.features)
      .Add("<filenames...>", "input wasm files",
           [&](string_view arg) { filenames.push_back(arg); });
  parser.Parse(args);

  if (filenames.empty()) {
    print("No filenames given.\n");
    parser.PrintHelpAndExit(1);
  }

  bool ok = true;
  for (auto filename : filenames) {
    auto optbuf = ReadFile(filename);
    if (!optbuf) {
      print(std::cerr, "Error reading file {}.\n", filename);
      ok = false;
      continue;
    }

    SpanU8 data{*optbuf};
    Tool tool{filename, data, options};
    bool valid = tool.Run();
    if (!valid || options.verbose) {
      print("[{:^4}] {}\n", valid ? "OK" : "FAIL", filename);
      tool.errors.PrintTo(std::cerr);
    }
    ok &= valid;
  }

  return ok ? 0 : 1;
}

Tool::Tool(string_view filename, SpanU8 data, Options options)
    : filename(filename),
      options{options},
      data{data},
      errors{data},
      module{ReadModule(data, options.features, errors)},
      visitor{options.features, errors} {}

bool Tool::Run() {
  if (module.magic && module.version) {
    visit::Visit(module, visitor);
  }
  return !errors.has_error();
}

}  // namespace validate
}  // namespace tools
}  // namespace wasp
