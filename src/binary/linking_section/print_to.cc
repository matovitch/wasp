//
// Copyright 2020 WebAssembly Community Group participants
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

#include <iostream>

#include "wasp/base/print_to_macros.h"
#include "wasp/binary/linking_section/formatters.h"
#include "wasp/binary/linking_section/types.h"

namespace wasp {
namespace binary {

WASP_BINARY_LINKING_ENUMS(WASP_DEFINE_PRINT_TO)
WASP_BINARY_LINKING_STRUCTS(WASP_DEFINE_PRINT_TO)

}  // namespace binary
}  // namespace wasp