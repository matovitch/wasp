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

#include "wasp/text/read.h"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <numeric>
#include <type_traits>

#include "wasp/base/errors.h"
#include "wasp/base/format.h"
#include "wasp/base/utf8.h"
#include "wasp/text/formatters.h"
#include "wasp/text/numeric.h"
#include "wasp/text/read/context.h"
#include "wasp/text/read/location_guard.h"
#include "wasp/text/read/macros.h"

namespace wasp {
namespace text {

auto Expect(Tokenizer& tokenizer, Context& context, TokenType expected)
    -> optional<Token> {
  auto actual_opt = tokenizer.Match(expected);
  if (!actual_opt) {
    auto token = tokenizer.Peek();
    context.errors.OnError(token.loc,
                           format("Expected {}, got {}", expected, token.type));
    return nullopt;
  }
  return actual_opt;
}

auto ExpectLpar(Tokenizer& tokenizer, Context& context, TokenType expected)
    -> optional<Token> {
  auto actual_opt = tokenizer.MatchLpar(expected);
  if (!actual_opt) {
    auto token = tokenizer.Peek();
    context.errors.OnError(
        token.loc, format("Expected '(' {}, got {} {}", expected, token.type,
                          tokenizer.Peek(1).type));
    return nullopt;
  }
  return actual_opt;
}

auto ReadNat32(Tokenizer& tokenizer, Context& context) -> OptAt<u32> {
  auto token_opt = tokenizer.Match(TokenType::Nat);
  if (!token_opt) {
    auto token = tokenizer.Peek();
    context.errors.OnError(
        token.loc, format("Expected a natural number, got {}", token.type));
    return nullopt;
  }
  auto nat_opt = StrToNat<u32>(token_opt->literal_info(), token_opt->span_u8());
  if (!nat_opt) {
    context.errors.OnError(
        token_opt->loc, format("Invalid natural number, got {}", *token_opt));
    return nullopt;
  }
  return MakeAt(token_opt->loc, *nat_opt);
}

template <typename T>
auto ReadInt(Tokenizer& tokenizer, Context& context) -> OptAt<T> {
  auto token = tokenizer.Peek();
  if (!(token.type == TokenType::Nat || token.type == TokenType::Int)) {
    context.errors.OnError(token.loc,
                           format("Expected an integer, got {}", token.type));
    return nullopt;
  }

  tokenizer.Read();
  auto int_opt = StrToInt<T>(token.literal_info(), token.span_u8());
  if (!int_opt) {
    context.errors.OnError(token.loc,
                           format("Invalid integer, got {}", token.type));
    return nullopt;
  }
  return MakeAt(token.loc, *int_opt);
}

template <typename T>
auto ReadFloat(Tokenizer& tokenizer, Context& context) -> OptAt<T> {
  auto token = tokenizer.Peek();
  if (!(token.type == TokenType::Nat || token.type == TokenType::Int ||
        token.type == TokenType::Float)) {
    context.errors.OnError(token.loc,
                           format("Expected a float, got {}", token.type));
    return nullopt;
  }

  tokenizer.Read();
  auto float_opt = StrToFloat<T>(token.literal_info(), token.span_u8());
  if (!float_opt) {
    context.errors.OnError(token.loc, format("Invalid float, got {}", token));
    return nullopt;
  }
  return MakeAt(token.loc, *float_opt);
}

auto ReadVar(Tokenizer& tokenizer, Context& context) -> OptAt<Var> {
  auto token = tokenizer.Peek();
  auto var_opt = ReadVarOpt(tokenizer, context);
  if (!var_opt) {
    context.errors.OnError(token.loc,
                           format("Expected a variable, got {}", token.type));
    return nullopt;
  }
  return *var_opt;
}

auto ReadVarOpt(Tokenizer& tokenizer, Context& context) -> OptAt<Var> {
  auto token = tokenizer.Peek();
  if (token.type == TokenType::Id) {
    tokenizer.Read();
    return MakeAt(token.loc, Var{token.as_string_view()});
  } else if (token.type == TokenType::Nat) {
    WASP_TRY_READ(nat, ReadNat32(tokenizer, context));
    return MakeAt(nat.loc(), Var{nat.value()});
  } else {
    return nullopt;
  }
}

auto ReadVarList(Tokenizer& tokenizer, Context& context) -> optional<VarList> {
  VarList result;
  OptAt<Var> var_opt;
  while ((var_opt = ReadVarOpt(tokenizer, context))) {
    result.push_back(*var_opt);
  }
  return result;
}

auto ReadNonEmptyVarList(Tokenizer& tokenizer, Context& context)
    -> optional<VarList> {
  VarList result;
  WASP_TRY_READ(var, ReadVar(tokenizer, context));
  result.push_back(var);

  WASP_TRY_READ(var_list, ReadVarList(tokenizer, context));
  result.insert(result.end(), std::make_move_iterator(var_list.begin()),
                std::make_move_iterator(var_list.end()));
  return result;
}

auto ReadVarUseOpt(Tokenizer& tokenizer, Context& context, TokenType token_type)
    -> OptAt<Var> {
  LocationGuard guard{tokenizer};
  if (!tokenizer.MatchLpar(token_type)) {
    return nullopt;
  }
  WASP_TRY_READ(var, ReadVar(tokenizer, context));
  WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  return MakeAt(guard.loc(), var.value());
}

auto ReadTypeUseOpt(Tokenizer& tokenizer, Context& context) -> OptAt<Var> {
  return ReadVarUseOpt(tokenizer, context, TokenType::Type);
}

auto ReadFunctionTypeUse(Tokenizer& tokenizer, Context& context)
    -> optional<FunctionTypeUse> {
  auto type_use = ReadTypeUseOpt(tokenizer, context);
  WASP_TRY_READ(type, ReadFunctionType(tokenizer, context));
  auto result = FunctionTypeUse{type_use, type};

  context.function_type_map.Use(result);
  return result;
}

auto ReadText(Tokenizer& tokenizer, Context& context) -> OptAt<Text> {
  auto token_opt = tokenizer.Match(TokenType::Text);
  if (!token_opt) {
    auto token = tokenizer.Peek();
    context.errors.OnError(token.loc,
                           format("Expected quoted text, got {}", token.type));
    return nullopt;
  }
  return MakeAt(token_opt->loc, token_opt->text());
}

auto ReadUtf8Text(Tokenizer& tokenizer, Context& context) -> OptAt<Text> {
  WASP_TRY_READ(text, ReadText(tokenizer, context));
  // TODO: The lexer could validate utf-8 while reading characters.
  if (!IsValidUtf8(text->ToString())) {
    context.errors.OnError(text.loc(), "Invalid UTF-8 encoding");
    return nullopt;
  }
  return text;
}

auto ReadTextList(Tokenizer& tokenizer, Context& context)
    -> optional<TextList> {
  TextList result;
  while (tokenizer.Peek().type == TokenType::Text) {
    WASP_TRY_READ(text, ReadText(tokenizer, context));
    result.push_back(text);
  }
  return result;
}

// Section 1: Type

auto ReadBindVarOpt(Tokenizer& tokenizer, Context& context, NameMap& name_map)
    -> OptAt<BindVar> {
  auto token_opt = tokenizer.Match(TokenType::Id);
  if (!token_opt) {
    name_map.NewUnbound();
    return nullopt;
  }

  auto name = token_opt->as_string_view();
  if (name_map.Has(name)) {
    context.errors.OnError(
        token_opt->loc, format("Variable {} is already bound to index {}", name,
                               name_map.Get(name)));

    // Use the previous name and treat this object as unbound.
    name_map.NewUnbound();
    return nullopt;
  }

  name_map.NewBound(name);
  return MakeAt(token_opt->loc, BindVar{name});
}

auto ReadBoundValueTypeList(Tokenizer& tokenizer,
                            Context& context,
                            NameMap& name_map,
                            TokenType token_type)
    -> optional<BoundValueTypeList> {
  BoundValueTypeList result;
  while (tokenizer.MatchLpar(token_type)) {
    if (tokenizer.Peek().type == TokenType::Id) {
      LocationGuard guard{tokenizer};
      auto bind_var_opt = ReadBindVarOpt(tokenizer, context, name_map);
      WASP_TRY_READ(value_type, ReadValueType(tokenizer, context));
      result.push_back(
          MakeAt(guard.loc(), BoundValueType{bind_var_opt, value_type}));
    } else {
      WASP_TRY_READ(value_types, ReadValueTypeList(tokenizer, context));
      for (auto& value_type : value_types) {
        result.push_back(
            MakeAt(value_type.loc(), BoundValueType{nullopt, value_type}));
      }
    }
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  }
  return result;
}

auto ReadBoundParamList(Tokenizer& tokenizer,
                        Context& context,
                        NameMap& name_map) -> optional<BoundValueTypeList> {
  return ReadBoundValueTypeList(tokenizer, context, name_map, TokenType::Param);
}

auto ReadUnboundValueTypeList(Tokenizer& tokenizer,
                              Context& context,
                              TokenType token_type) -> optional<ValueTypeList> {
  ValueTypeList result;
  while (tokenizer.MatchLpar(token_type)) {
    WASP_TRY_READ(value_types, ReadValueTypeList(tokenizer, context));
    std::copy(value_types.begin(), value_types.end(),
              std::back_inserter(result));
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  }
  return result;
}

auto ReadParamList(Tokenizer& tokenizer, Context& context)
    -> optional<ValueTypeList> {
  return ReadUnboundValueTypeList(tokenizer, context, TokenType::Param);
}

auto ReadResultList(Tokenizer& tokenizer, Context& context)
    -> optional<ValueTypeList> {
  return ReadUnboundValueTypeList(tokenizer, context, TokenType::Result);
}

auto ReadValueType(Tokenizer& tokenizer, Context& context) -> OptAt<ValueType> {
  auto token_opt = tokenizer.Match(TokenType::ValueType);
  if (!token_opt) {
    auto token = tokenizer.Peek();
    context.errors.OnError(token.loc,
                           format("Expected value type, got {}", token.type));
    return nullopt;
  }
  bool allowed = true;
  switch (token_opt->value_type()) {
#define WASP_V(val, Name, str)
#define WASP_FEATURE_V(val, Name, str, feature)  \
  case ValueType::Name:                          \
    if (!context.features.feature##_enabled()) { \
      allowed = false;                           \
    }                                            \
    break;
#include "wasp/base/def/value_type.def"
#undef WASP_V
#undef WASP_FEATURE_V
    default:
      break;
  }
  if (!allowed) {
    context.errors.OnError(token_opt->loc, format("value type {} not allowed",
                                                  token_opt->value_type()));
    return nullopt;
  }
  return token_opt->value_type();
}

auto ReadValueTypeList(Tokenizer& tokenizer, Context& context)
    -> optional<ValueTypeList> {
  ValueTypeList result;
  while (tokenizer.Peek().type == TokenType::ValueType) {
    WASP_TRY_READ(value, ReadValueType(tokenizer, context));
    result.push_back(value);
  }
  return result;
}

auto ReadBoundFunctionType(Tokenizer& tokenizer,
                           Context& context,
                           NameMap& name_map) -> OptAt<BoundFunctionType> {
  LocationGuard guard{tokenizer};
  WASP_TRY_READ(params, ReadBoundParamList(tokenizer, context, name_map));
  WASP_TRY_READ(results, ReadResultList(tokenizer, context));
  return MakeAt(guard.loc(), BoundFunctionType{params, results});
}

auto ReadTypeEntry(Tokenizer& tokenizer, Context& context) -> OptAt<TypeEntry> {
  LocationGuard guard{tokenizer};
  WASP_TRY(ExpectLpar(tokenizer, context, TokenType::Type));
  auto bind_var = ReadBindVarOpt(tokenizer, context, context.type_names);
  WASP_TRY(ExpectLpar(tokenizer, context, TokenType::Func));

  NameMap dummy_name_map;  // Bound names are not used.
  WASP_TRY_READ(type,
                ReadBoundFunctionType(tokenizer, context, dummy_name_map));
  context.function_type_map.Define(type);

  WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  return MakeAt(guard.loc(), TypeEntry{bind_var, type});
}

// Section 2: Import

auto ReadInlineImportOpt(Tokenizer& tokenizer, Context& context)
    -> OptAt<InlineImport> {
  LocationGuard guard{tokenizer};
  auto import_token = tokenizer.MatchLpar(TokenType::Import);
  if (!import_token) {
    return nullopt;
  }

  if (context.seen_non_import) {
    context.errors.OnError(
        import_token->loc,
        "Imports must occur before all non-import definitions");
    return nullopt;
  }
  WASP_TRY_READ(module, ReadUtf8Text(tokenizer, context));
  WASP_TRY_READ(name, ReadUtf8Text(tokenizer, context));
  WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  return MakeAt(guard.loc(), InlineImport{module, name});
}

auto ReadImport(Tokenizer& tokenizer, Context& context) -> OptAt<Import> {
  LocationGuard guard{tokenizer};
  WASP_TRY_READ(import_token,
                ExpectLpar(tokenizer, context, TokenType::Import));

  if (context.seen_non_import) {
    context.errors.OnError(
        import_token.loc,
        "Imports must occur before all non-import definitions");
    return nullopt;
  }

  Import result;
  WASP_TRY_READ(module, ReadUtf8Text(tokenizer, context));
  WASP_TRY_READ(name, ReadUtf8Text(tokenizer, context));
  result.module = module;
  result.name = name;

  WASP_TRY(Expect(tokenizer, context, TokenType::Lpar));

  auto token = tokenizer.Peek();
  switch (token.type) {
    case TokenType::Func: {
      tokenizer.Read();
      auto name = ReadBindVarOpt(tokenizer, context, context.function_names);
      auto type_use = ReadTypeUseOpt(tokenizer, context);
      NameMap dummy_name_map;  // Bound names are not used.
      WASP_TRY_READ(type,
                    ReadBoundFunctionType(tokenizer, context, dummy_name_map));
      context.function_type_map.Use(type_use, type);
      result.desc = FunctionDesc{name, type_use, type};
      break;
    }

    case TokenType::Table: {
      tokenizer.Read();
      auto name = ReadBindVarOpt(tokenizer, context, context.table_names);
      WASP_TRY_READ(type, ReadTableType(tokenizer, context));
      result.desc = TableDesc{name, type};
      break;
    }

    case TokenType::Memory: {
      tokenizer.Read();
      auto name = ReadBindVarOpt(tokenizer, context, context.memory_names);
      WASP_TRY_READ(type, ReadMemoryType(tokenizer, context));
      result.desc = MemoryDesc{name, type};
      break;
    }

    case TokenType::Global: {
      tokenizer.Read();
      auto name = ReadBindVarOpt(tokenizer, context, context.global_names);
      WASP_TRY_READ(type, ReadGlobalType(tokenizer, context));
      result.desc = GlobalDesc{name, type};
      break;
    }

    case TokenType::Event: {
      if (!context.features.exceptions_enabled()) {
        context.errors.OnError(token.loc, "Events not allowed");
        return nullopt;
      }
      tokenizer.Read();
      auto name = ReadBindVarOpt(tokenizer, context, context.event_names);
      WASP_TRY_READ(type, ReadEventType(tokenizer, context));
      result.desc = EventDesc{name, type};
      break;
    }

    default:
      context.errors.OnError(
          token.loc, format("Expected an import external kind, got {}", token));
      return nullopt;
  }

  WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  return MakeAt(guard.loc(), result);
}

// Section 3: Function

auto ReadLocalList(Tokenizer& tokenizer, Context& context, NameMap& name_map)
    -> optional<BoundValueTypeList> {
  return ReadBoundValueTypeList(tokenizer, context, name_map, TokenType::Local);
}

auto ReadFunctionType(Tokenizer& tokenizer, Context& context)
    -> OptAt<FunctionType> {
  LocationGuard guard{tokenizer};
  WASP_TRY_READ(params, ReadParamList(tokenizer, context));
  WASP_TRY_READ(results, ReadResultList(tokenizer, context));
  return MakeAt(guard.loc(), FunctionType{params, results});
}

auto ReadFunction(Tokenizer& tokenizer, Context& context) -> OptAt<Function> {
  LocationGuard guard{tokenizer};
  WASP_TRY(ExpectLpar(tokenizer, context, TokenType::Func));

  BoundValueTypeList locals;
  InstructionList instructions;

  auto name = ReadBindVarOpt(tokenizer, context, context.function_names);
  WASP_TRY_READ(exports, ReadInlineExportList(tokenizer, context));
  auto import_opt = ReadInlineImportOpt(tokenizer, context);
  context.seen_non_import |= !import_opt;

  context.local_names.Reset();

  auto type_use = ReadTypeUseOpt(tokenizer, context);
  WASP_TRY_READ(type,
                ReadBoundFunctionType(tokenizer, context, context.local_names));
  context.function_type_map.Use(type_use, type);
  if (!import_opt) {
    WASP_TRY_READ(locals_,
                  ReadLocalList(tokenizer, context, context.local_names));
    locals = locals_;
    WASP_TRY(ReadInstructionList(tokenizer, context, instructions));
  }

  WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  return MakeAt(guard.loc(),
                Function{FunctionDesc{name, type_use, type}, locals,
                         instructions, import_opt, exports});
}

// Section 4: Table

auto ReadLimits(Tokenizer& tokenizer, Context& context) -> OptAt<Limits> {
  LocationGuard guard{tokenizer};
  WASP_TRY_READ(min, ReadNat32(tokenizer, context));
  auto token = tokenizer.Peek();
  OptAt<u32> max_opt;
  if (token.type == TokenType::Nat) {
    WASP_TRY_READ(max, ReadNat32(tokenizer, context));
    max_opt = max;
  }

  token = tokenizer.Peek();
  At<Shared> shared = Shared::No;
  if (token.type == TokenType::Shared) {
    tokenizer.Read();
    shared = MakeAt(token.loc, Shared::Yes);
  }

  return MakeAt(guard.loc(), Limits{min, max_opt, shared});
}

auto ReadReferenceKind(Tokenizer& tokenizer, Context& context)
    -> OptAt<ReferenceType> {
  auto token = tokenizer.Peek();
  if (!token.has_reference_type()) {
    context.errors.OnError(
        token.loc, format("Expected reference type, got {}", token.type));
    return nullopt;
  }

  tokenizer.Read();
  return token.reference_type();
}

auto ReadReferenceType(Tokenizer& tokenizer, Context& context)
    -> OptAt<ReferenceType> {
  auto token = tokenizer.Peek();
  auto reftype = ReadReferenceTypeOpt(tokenizer, context);
  if (!reftype) {
    context.errors.OnError(
        token.loc, format("Expected reference type, got {}", token.type));
    return nullopt;
  }

  return *reftype;
}

auto ReadReferenceTypeOpt(Tokenizer& tokenizer, Context& context)
    -> OptAt<ReferenceType> {
  auto token_opt = tokenizer.Match(TokenType::ValueType);
  if (!token_opt) {
    return nullopt;
  }

  bool allowed = true;
  ReferenceType reftype;

  switch (token_opt->value_type()) {
#define WASP_V(val, Name, str)     \
  case ValueType::Name:            \
    reftype = ReferenceType::Name; \
    break;
#define WASP_FEATURE_V(val, Name, str, feature)  \
  case ValueType::Name:                          \
    reftype = ReferenceType::Name;               \
    if (!context.features.feature##_enabled()) { \
      allowed = false;                           \
    }                                            \
    break;
#include "wasp/base/def/reference_type.def"
#undef WASP_V
#undef WASP_FEATURE_V
    default:
      context.errors.OnError(
          token_opt->loc,
          format("{} is not a reference type", token_opt->value_type()));
      return nullopt;
  }
  if (!allowed) {
    context.errors.OnError(token_opt->loc,
                           format("reference type {} not allowed", reftype));
    return nullopt;
  }
  return MakeAt(token_opt->loc, reftype);
}

auto ReadTableType(Tokenizer& tokenizer, Context& context) -> OptAt<TableType> {
  LocationGuard guard{tokenizer};
  WASP_TRY_READ(limits, ReadLimits(tokenizer, context));
  WASP_TRY_READ(element, ReadReferenceType(tokenizer, context));
  return MakeAt(guard.loc(), TableType{limits, element});
}

auto ReadTable(Tokenizer& tokenizer, Context& context) -> OptAt<Table> {
  LocationGuard guard{tokenizer};
  WASP_TRY(ExpectLpar(tokenizer, context, TokenType::Table));

  auto name = ReadBindVarOpt(tokenizer, context, context.table_names);
  WASP_TRY_READ(exports, ReadInlineExportList(tokenizer, context));
  auto import_opt = ReadInlineImportOpt(tokenizer, context);
  context.seen_non_import |= !import_opt;

  auto elemtype_opt = ReadReferenceTypeOpt(tokenizer, context);
  if (import_opt) {
    // Imported table.
    WASP_TRY_READ(type, ReadTableType(tokenizer, context));
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
    return MakeAt(guard.loc(),
                  Table{TableDesc{name, type}, *import_opt, exports});
  } else if (elemtype_opt) {
    // Inline element segment.
    WASP_TRY(ExpectLpar(tokenizer, context, TokenType::Elem));

    ElementList elements;
    u32 size;
    if (context.features.bulk_memory_enabled() && IsExpression(tokenizer)) {
      // Element expression list.
      WASP_TRY_READ(expressions, ReadElementExpressionList(tokenizer, context));
      size = expressions.size();
      elements =
          ElementList{ElementListWithExpressions{*elemtype_opt, expressions}};
    } else {
      // Element var list.
      WASP_TRY_READ(vars, ReadVarList(tokenizer, context));
      size = vars.size();
      elements = ElementList{ElementListWithVars{ExternalKind::Function, vars}};
    }

    // Implicit table type.
    auto type = TableType{Limits{size, size}, *elemtype_opt};

    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
    return MakeAt(guard.loc(), Table{TableDesc{name, type}, exports, elements});
  } else {
    // Defined table.
    WASP_TRY_READ(type, ReadTableType(tokenizer, context));
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
    return MakeAt(guard.loc(), Table{TableDesc{name, type}, exports});
  }
}

// Section 5: Memory

auto ReadMemoryType(Tokenizer& tokenizer, Context& context)
    -> OptAt<MemoryType> {
  WASP_TRY_READ(limits, ReadLimits(tokenizer, context));
  return MakeAt(limits.loc(), MemoryType{limits});
}

auto ReadMemory(Tokenizer& tokenizer, Context& context) -> OptAt<Memory> {
  LocationGuard guard{tokenizer};
  WASP_TRY(ExpectLpar(tokenizer, context, TokenType::Memory));

  auto name = ReadBindVarOpt(tokenizer, context, context.memory_names);
  WASP_TRY_READ(exports, ReadInlineExportList(tokenizer, context));
  auto import_opt = ReadInlineImportOpt(tokenizer, context);
  context.seen_non_import |= !import_opt;

  if (import_opt) {
    // Imported memory.
    WASP_TRY_READ(type, ReadMemoryType(tokenizer, context));
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
    return MakeAt(guard.loc(),
                  Memory{MemoryDesc{name, type}, *import_opt, exports});
  } else if (tokenizer.MatchLpar(TokenType::Data)) {
    // Inline data segment.
    WASP_TRY_READ(data, ReadTextList(tokenizer, context));
    auto size = std::accumulate(
        data.begin(), data.end(), u32{0},
        [](u32 total, Text text) { return total + text.byte_size; });

    // Implicit memory type.
    auto type = MemoryType{Limits{size, size}};

    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
    return MakeAt(guard.loc(),
                  Memory{MemoryDesc{name, type}, exports, data});
  } else {
    // Defined memory.
    WASP_TRY_READ(type, ReadMemoryType(tokenizer, context));
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
    return MakeAt(guard.loc(), Memory{MemoryDesc{name, type}, exports});
  }
}

// Section 6: Global

auto ReadConstantExpression(Tokenizer& tokenizer, Context& context)
    -> OptAt<ConstantExpression> {
  LocationGuard guard{tokenizer};
  InstructionList instructions;
  WASP_TRY(ReadInstructionList(tokenizer, context, instructions));
  return MakeAt(guard.loc(), ConstantExpression{instructions});
}

auto ReadGlobalType(Tokenizer& tokenizer, Context& context)
    -> OptAt<GlobalType> {
  LocationGuard guard{tokenizer};

  auto token_opt = tokenizer.MatchLpar(TokenType::Mut);
  WASP_TRY_READ(valtype, ReadValueType(tokenizer, context));

  At<Mutability> mut;
  if (token_opt) {
    mut = MakeAt(token_opt->loc, Mutability::Var);
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  } else {
    mut = Mutability::Const;
  }
  return MakeAt(guard.loc(), GlobalType{valtype, mut});
}

auto ReadGlobal(Tokenizer& tokenizer, Context& context) -> OptAt<Global> {
  LocationGuard guard{tokenizer};
  WASP_TRY(ExpectLpar(tokenizer, context, TokenType::Global));

  auto name = ReadBindVarOpt(tokenizer, context, context.global_names);
  WASP_TRY_READ(exports, ReadInlineExportList(tokenizer, context));
  auto import_opt = ReadInlineImportOpt(tokenizer, context);
  context.seen_non_import |= !import_opt;

  WASP_TRY_READ(type, ReadGlobalType(tokenizer, context));

  At<ConstantExpression> init;
  if (!import_opt) {
    WASP_TRY_READ(init_, ReadConstantExpression(tokenizer, context));
    init = init_;
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
    return MakeAt(guard.loc(), Global{GlobalDesc{name, type}, init, exports});
  }

  WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  return MakeAt(guard.loc(),
                Global{GlobalDesc{name, type}, *import_opt, exports});
}

// Section 7: Export

auto ReadInlineExport(Tokenizer& tokenizer, Context& context)
    -> OptAt<InlineExport> {
  LocationGuard guard{tokenizer};
  WASP_TRY(ExpectLpar(tokenizer, context, TokenType::Export));
  WASP_TRY_READ(name, ReadUtf8Text(tokenizer, context));
  WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  return MakeAt(guard.loc(), InlineExport{name});
}

auto ReadInlineExportList(Tokenizer& tokenizer, Context& context)
    -> optional<InlineExportList> {
  InlineExportList result;
  while (tokenizer.Peek().type == TokenType::Lpar &&
         tokenizer.Peek(1).type == TokenType::Export) {
    WASP_TRY_READ(export_, ReadInlineExport(tokenizer, context));
    result.push_back(export_);
  }
  return result;
}

auto ReadExport(Tokenizer& tokenizer, Context& context) -> OptAt<Export> {
  LocationGuard guard{tokenizer};
  WASP_TRY(ExpectLpar(tokenizer, context, TokenType::Export));

  WASP_TRY_READ(name, ReadUtf8Text(tokenizer, context));

  At<ExternalKind> kind;

  WASP_TRY(Expect(tokenizer, context, TokenType::Lpar));
  auto token = tokenizer.Peek();
  switch (token.type) {
    case TokenType::Func:
      kind = MakeAt(token.loc, ExternalKind::Function);
      break;

    case TokenType::Table:
      kind = MakeAt(token.loc, ExternalKind::Table);
      break;

    case TokenType::Memory:
      kind = MakeAt(token.loc, ExternalKind::Memory);
      break;

    case TokenType::Global:
      kind = MakeAt(token.loc, ExternalKind::Global);
      break;

    case TokenType::Event:
      if (!context.features.exceptions_enabled()) {
        context.errors.OnError(token.loc, "Events not allowed");
        return nullopt;
      }
      kind = MakeAt(token.loc, ExternalKind::Event);
      break;

    default:
      context.errors.OnError(
          token.loc,
          format("Expected an import external kind, got {}", token.type));
      return nullopt;
  }

  tokenizer.Read();
  WASP_TRY_READ(var, ReadVar(tokenizer, context));

  WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));

  return MakeAt(guard.loc(), Export{kind, name, var});
}

// Section 8: Start

auto ReadStart(Tokenizer& tokenizer, Context& context) -> OptAt<Start> {
  LocationGuard guard{tokenizer};
  WASP_TRY_READ(start_token, ExpectLpar(tokenizer, context, TokenType::Start));

  if (context.seen_start) {
    context.errors.OnError(start_token.loc, "Multiple start functions");
    return nullopt;
  }
  context.seen_start = true;

  WASP_TRY_READ(var, ReadVar(tokenizer, context));
  WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  return MakeAt(guard.loc(), Start{var});
}

// Section 9: Elem

auto ReadOffsetExpression(Tokenizer& tokenizer, Context& context)
    -> OptAt<ConstantExpression> {
  LocationGuard guard{tokenizer};
  InstructionList instructions;
  if (tokenizer.MatchLpar(TokenType::Offset)) {
    WASP_TRY(ReadInstructionList(tokenizer, context, instructions));
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  } else if (IsExpression(tokenizer)) {
    WASP_TRY(ReadExpression(tokenizer, context, instructions));
  } else {
    auto token = tokenizer.Peek();
    context.errors.OnError(
        token.loc, format("Expected offset expression, got {}", token.type));
    return nullopt;
  }
  return MakeAt(guard.loc(), ConstantExpression{instructions});
}

auto ReadElementExpression(Tokenizer& tokenizer, Context& context)
    -> OptAt<ElementExpression> {
  LocationGuard guard{tokenizer};
  InstructionList instructions;

  // Element expressions were first added in the bulk memory proposal, so it
  // shouldn't be read (and this function shouldn't be called) if that feature
  // is not enabled.
  assert(context.features.bulk_memory_enabled());
  // The only valid instructions are enabled by the reference types proposal,
  // but their encoding is still used by the bulk memory proposal.
  Features new_features;
  new_features.enable_reference_types();
  Context new_context{new_features, context.errors};

  if (tokenizer.MatchLpar(TokenType::Item)) {
    WASP_TRY(ReadInstructionList(tokenizer, new_context, instructions));
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  } else if (IsExpression(tokenizer)) {
    WASP_TRY(ReadExpression(tokenizer, new_context, instructions));
  } else {
    auto token = tokenizer.Peek();
    context.errors.OnError(
        token.loc, format("Expected element expression, got {}", token.type));
    return nullopt;
  }
  return MakeAt(guard.loc(), ElementExpression{instructions});
}

auto ReadElementExpressionList(Tokenizer& tokenizer, Context& context)
    -> optional<ElementExpressionList> {
  ElementExpressionList result;
  while (IsElementExpression(tokenizer)) {
    WASP_TRY_READ(expression, ReadElementExpression(tokenizer, context));
    result.push_back(expression);
  }
  return result;
}

auto ReadTableUseOpt(Tokenizer& tokenizer, Context& context) -> OptAt<Var> {
  return ReadVarUseOpt(tokenizer, context, TokenType::Table);
}

auto ReadElementSegment(Tokenizer& tokenizer, Context& context)
    -> OptAt<ElementSegment> {
  LocationGuard guard{tokenizer};
  WASP_TRY(ExpectLpar(tokenizer, context, TokenType::Elem));

  if (context.features.bulk_memory_enabled()) {
    // LPAR ELEM * bind_var_opt elem_list RPAR
    // LPAR ELEM * bind_var_opt table_use offset elem_list RPAR
    // LPAR ELEM * bind_var_opt DECLARE elem_list RPAR
    // LPAR ELEM * bind_var_opt offset elem_list RPAR  /* Sugar */
    // LPAR ELEM * bind_var_opt offset elem_var_list RPAR  /* Sugar */
    auto name =
        ReadBindVarOpt(tokenizer, context, context.element_segment_names);
    auto table_use_opt = ReadTableUseOpt(tokenizer, context);

    SegmentType segment_type;
    OptAt<ConstantExpression> offset_opt;
    if (table_use_opt) {
      // LPAR ELEM bind_var_opt table_use * offset elem_list RPAR
      WASP_TRY_READ(offset, ReadOffsetExpression(tokenizer, context));
      offset_opt = offset;
      segment_type = SegmentType::Active;
    } else {
      auto token = tokenizer.Peek();
      if (token.type == TokenType::Declare) {
        // LPAR ELEM bind_var_opt * DECLARE elem_list RPAR
        tokenizer.Read();
        segment_type = SegmentType::Declared;
      } else if (token.type == TokenType::Lpar) {
        segment_type = SegmentType::Active;
        // LPAR ELEM bind_var_opt * offset elem_list RPAR
        // LPAR ELEM bind_var_opt * offset elem_var_list RPAR
        WASP_TRY_READ(offset, ReadOffsetExpression(tokenizer, context));
        offset_opt = offset;

        token = tokenizer.Peek();
        if (token.type == TokenType::Nat || token.type == TokenType::Id ||
            token.type == TokenType::Rpar) {
          // LPAR ELEM bind_var_opt offset * elem_var_list RPAR
          WASP_TRY_READ(init, ReadVarList(tokenizer, context));
          WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
          return MakeAt(guard.loc(),
                        ElementSegment{
                            name, nullopt, *offset_opt,
                            ElementListWithVars{ExternalKind::Function, init}});
        }

        // LPAR ELEM bind_var_opt offset * elem_list RPAR
      } else {
        // LPAR ELEM bind_var_opt * elem_list RPAR
        segment_type = SegmentType::Passive;
      }
    }
    // ... * elem_list RPAR
    auto token = tokenizer.Peek();
    if (token.type == TokenType::Func) {
      tokenizer.Read();
      // * elem_kind elem_var_list
      auto kind = MakeAt(token.loc, ExternalKind::Function);
      WASP_TRY_READ(init, ReadVarList(tokenizer, context));
      WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));

      if (segment_type == SegmentType::Active) {
        assert(offset_opt.has_value());
        return MakeAt(guard.loc(),
                      ElementSegment{name, table_use_opt, *offset_opt,
                                     ElementListWithVars{kind, init}});
      } else {
        return MakeAt(guard.loc(),
                      ElementSegment{name, segment_type,
                                     ElementListWithVars{kind, init}});
      }
    } else {
      // * ref_type elem_expr_list
      WASP_TRY_READ(elemtype, ReadReferenceType(tokenizer, context));
      WASP_TRY_READ(init, ReadElementExpressionList(tokenizer, context));
      WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));

      if (segment_type == SegmentType::Active) {
        assert(offset_opt.has_value());
        return MakeAt(
            guard.loc(),
            ElementSegment{name, table_use_opt, *offset_opt,
                           ElementListWithExpressions{elemtype, init}});
      } else {
        return MakeAt(guard.loc(), ElementSegment{name, segment_type,
                                                  ElementListWithExpressions{
                                                      elemtype, init}});
      }
    }
  } else {
    // LPAR ELEM * var offset var_list RPAR
    // LPAR ELEM * offset var_list RPAR  /* Sugar */
    auto table = ReadVarOpt(tokenizer, context);
    WASP_TRY_READ(offset, ReadOffsetExpression(tokenizer, context));
    WASP_TRY_READ(init, ReadVarList(tokenizer, context));
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
    return MakeAt(
        guard.loc(),
        ElementSegment{nullopt, table, offset,
                       ElementListWithVars{ExternalKind::Function, init}});
  }
}

// Section 10: Code

auto ReadNameEqNatOpt(Tokenizer& tokenizer,
                      Context& context,
                      TokenType token_type,
                      u32 offset) -> OptAt<u32> {
  auto token_opt = tokenizer.Match(token_type);
  if (!token_opt) {
    return nullopt;
  }

  auto nat_opt = StrToNat<u32>(token_opt->literal_info(),
                               token_opt->span_u8().subspan(offset));
  if (!nat_opt) {
    context.errors.OnError(
        token_opt->loc,
        format("Invalid natural number, got {}", token_opt->type));
    return nullopt;
  }

  return MakeAt(token_opt->loc, *nat_opt);
}

auto ReadAlignOpt(Tokenizer& tokenizer, Context& context) -> OptAt<u32> {
  auto nat_opt = ReadNameEqNatOpt(tokenizer, context, TokenType::AlignEqNat, 6);
  if (!nat_opt) {
    return nullopt;
  }

  auto value = nat_opt->value();
  if (value == 0 || (value & (value - 1)) != 0) {
    context.errors.OnError(
        nat_opt->loc(),
        format("Alignment must be a power of two, got {}", value));
    return nullopt;
  }
  return nat_opt;
}

auto ReadOffsetOpt(Tokenizer& tokenizer, Context& context) -> OptAt<u32> {
  return ReadNameEqNatOpt(tokenizer, context, TokenType::OffsetEqNat, 7);
}

auto ReadSimdLane(Tokenizer& tokenizer, Context& context) -> OptAt<u8> {
  // TODO: This should probably be ReadNat<u8>, but the simd tests currently
  // allow signed values here.
  auto token = tokenizer.Peek();
  if (token.type == TokenType::Int &&
      token.literal_info().sign == Sign::Minus) {
    context.errors.OnError(
        token.loc, format("Expected a positive integer, got {}", token.type));
    return nullopt;
  }
  return ReadInt<u8>(tokenizer, context);
}

auto ReadSimdShuffleImmediate(Tokenizer& tokenizer, Context& context)
    -> OptAt<ShuffleImmediate> {
  LocationGuard guard{tokenizer};
  ShuffleImmediate result;
  for (size_t lane = 0; lane < result.size(); ++lane) {
    WASP_TRY_READ(value, ReadSimdLane(tokenizer, context));
    result[lane] = value;
  }
  return MakeAt(guard.loc(), result);
}

template <typename T, size_t N>
auto ReadSimdValues(Tokenizer& tokenizer, Context& context) -> OptAt<v128> {
  LocationGuard guard{tokenizer};
  std::array<T, N> result;
  for (size_t lane = 0; lane < N; ++lane) {
    if constexpr (std::is_floating_point_v<T>) {
      WASP_TRY_READ(float_, ReadFloat<T>(tokenizer, context));
      result[lane] = float_;
    } else {
      WASP_TRY_READ(int_, ReadInt<T>(tokenizer, context));
      result[lane] = int_;
    }
  }
  return MakeAt(guard.loc(), v128{result});
}

bool IsPlainInstruction(Token token) {
  switch (token.type) {
    case TokenType::BareInstr:
    case TokenType::BrOnExnInstr:
    case TokenType::BrTableInstr:
    case TokenType::CallIndirectInstr:
    case TokenType::F32ConstInstr:
    case TokenType::F64ConstInstr:
    case TokenType::I32ConstInstr:
    case TokenType::I64ConstInstr:
    case TokenType::MemoryInstr:
    case TokenType::MemoryCopyInstr:
    case TokenType::MemoryInitInstr:
    case TokenType::RefFuncInstr:
    case TokenType::RefNullInstr:
    case TokenType::SelectInstr:
    case TokenType::SimdConstInstr:
    case TokenType::SimdLaneInstr:
    case TokenType::SimdShuffleInstr:
    case TokenType::TableCopyInstr:
    case TokenType::TableInitInstr:
    case TokenType::VarInstr:
      return true;

    default:
      return false;
  }
}

bool IsBlockInstruction(Token token) {
  return token.type == TokenType::BlockInstr;
}

bool IsExpression(Tokenizer& tokenizer) {
  return tokenizer.Peek().type == TokenType::Lpar &&
         (IsPlainInstruction(tokenizer.Peek(1)) ||
          IsBlockInstruction(tokenizer.Peek(1)));
}

bool IsInstruction(Tokenizer& tokenizer) {
  auto token = tokenizer.Peek();
  return IsPlainInstruction(token) || IsBlockInstruction(token) ||
         IsExpression(tokenizer);
}

bool IsElementExpression(Tokenizer& tokenizer) {
  return IsExpression(tokenizer) || (tokenizer.Peek().type == TokenType::Lpar &&
                                     tokenizer.Peek(1).type == TokenType::Item);
}

bool CheckOpcodeEnabled(Token token, Context& context) {
  assert(token.has_opcode());
  if (!context.features.HasFeatures(Features{token.opcode_features()})) {
    context.errors.OnError(
        token.loc, format("{} instruction not allowed", token.opcode()));
    return false;
  }
  return true;
}

auto ReadPlainInstruction(Tokenizer& tokenizer, Context& context)
    -> OptAt<Instruction> {
  LocationGuard guard{tokenizer};
  auto token = tokenizer.Peek();
  switch (token.type) {
    case TokenType::BareInstr:
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      return MakeAt(token.loc, Instruction{token.opcode()});

    case TokenType::RefNullInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      WASP_TRY_READ(type, ReadReferenceKind(tokenizer, context));
      return MakeAt(guard.loc(), Instruction{token.opcode(), type});
    }

    case TokenType::BrOnExnInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      LocationGuard immediate_guard{tokenizer};
      WASP_TRY_READ(label_var, ReadVar(tokenizer, context));
      WASP_TRY_READ(exn_var, ReadVar(tokenizer, context));
      auto immediate =
          MakeAt(immediate_guard.loc(), BrOnExnImmediate{label_var, exn_var});
      return MakeAt(guard.loc(), Instruction{token.opcode(), immediate});
    }

    case TokenType::BrTableInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      LocationGuard immediate_guard{tokenizer};
      WASP_TRY_READ(var_list, ReadNonEmptyVarList(tokenizer, context));
      auto default_target = var_list.back();
      var_list.pop_back();
      auto immediate = MakeAt(immediate_guard.loc(),
                              BrTableImmediate{var_list, default_target});
      return MakeAt(guard.loc(), Instruction{token.opcode(), immediate});
    }

    case TokenType::CallIndirectInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      LocationGuard immediate_guard{tokenizer};
      OptAt<Var> table_var_opt;
      if (context.features.reference_types_enabled()) {
        table_var_opt = ReadVarOpt(tokenizer, context);
      }
      WASP_TRY_READ(type, ReadFunctionTypeUse(tokenizer, context));
      auto immediate = MakeAt(immediate_guard.loc(),
                              CallIndirectImmediate{table_var_opt, type});
      return MakeAt(guard.loc(), Instruction{token.opcode(), immediate});
    }

    case TokenType::F32ConstInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      WASP_TRY_READ(immediate, ReadFloat<f32>(tokenizer, context));
      return MakeAt(guard.loc(), Instruction{token.opcode(), immediate});
    }

    case TokenType::F64ConstInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      WASP_TRY_READ(immediate, ReadFloat<f64>(tokenizer, context));
      return MakeAt(guard.loc(), Instruction{token.opcode(), immediate});
    }

    case TokenType::I32ConstInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      WASP_TRY_READ(immediate, ReadInt<s32>(tokenizer, context));
      return MakeAt(guard.loc(), Instruction{token.opcode(), immediate});
    }

    case TokenType::I64ConstInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      WASP_TRY_READ(immediate, ReadInt<s64>(tokenizer, context));
      return MakeAt(guard.loc(), Instruction{token.opcode(), immediate});
    }

    case TokenType::MemoryInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      LocationGuard immediate_guard{tokenizer};
      auto offset_opt = ReadOffsetOpt(tokenizer, context);
      auto align_opt = ReadAlignOpt(tokenizer, context);
      auto immediate =
          MakeAt(immediate_guard.loc(), MemArgImmediate{align_opt, offset_opt});
      return MakeAt(guard.loc(), Instruction{token.opcode(), immediate});
    }

    case TokenType::MemoryCopyInstr:
      CheckOpcodeEnabled(token, context);
      tokenizer.Read();
      return MakeAt(guard.loc(), Instruction{token.opcode(), CopyImmediate{}});

    case TokenType::MemoryInitInstr: {
      CheckOpcodeEnabled(token, context);
      tokenizer.Read();
      LocationGuard immediate_guard{tokenizer};
      WASP_TRY_READ(segment_var, ReadVar(tokenizer, context));
      auto immediate =
          MakeAt(immediate_guard.loc(), InitImmediate{segment_var, nullopt});
      return MakeAt(guard.loc(), Instruction{token.opcode(), immediate});
    }

    case TokenType::SelectInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      At<Opcode> opcode = token.opcode();
      At<ValueTypeList> immediate;
      if (context.features.reference_types_enabled()) {
        LocationGuard immediate_guard{tokenizer};
        WASP_TRY_READ(value_type_list, ReadResultList(tokenizer, context));
        immediate = MakeAt(immediate_guard.loc(), value_type_list);
        if (!value_type_list.empty()) {
          // Typed select has a different opcode.
          opcode = MakeAt(opcode.loc(), Opcode::SelectT);
        }
      }
      return MakeAt(guard.loc(), Instruction{opcode, immediate});
    }

    case TokenType::SimdConstInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      auto simd_token = tokenizer.Peek();

      OptAt<v128> immediate_opt;
      switch (simd_token.type) {
        case TokenType::I8X16:
          tokenizer.Read();
          immediate_opt = ReadSimdValues<u8, 16>(tokenizer, context);
          break;

        case TokenType::I16X8:
          tokenizer.Read();
          immediate_opt = ReadSimdValues<u16, 8>(tokenizer, context);
          break;

        case TokenType::I32X4:
          tokenizer.Read();
          immediate_opt = ReadSimdValues<u32, 4>(tokenizer, context);
          break;

        case TokenType::I64X2:
          tokenizer.Read();
          immediate_opt = ReadSimdValues<u64, 2>(tokenizer, context);
          break;

        case TokenType::F32X4:
          tokenizer.Read();
          immediate_opt = ReadSimdValues<f32, 4>(tokenizer, context);
          break;

        case TokenType::F64X2:
          tokenizer.Read();
          immediate_opt = ReadSimdValues<f64, 2>(tokenizer, context);
          break;

        default:
          break;
      }

      if (!immediate_opt) {
        context.errors.OnError(
            token.loc,
            format("Invalid SIMD constant token, got {}", simd_token.type));
        return nullopt;
      }

      return MakeAt(guard.loc(), Instruction{token.opcode(), *immediate_opt});
    }

    case TokenType::SimdLaneInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      WASP_TRY_READ(immediate, ReadSimdLane(tokenizer, context));
      return MakeAt(guard.loc(), Instruction{token.opcode(), immediate});
    }

    case TokenType::SimdShuffleInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      WASP_TRY_READ(immediate, ReadSimdShuffleImmediate(tokenizer, context));
      return MakeAt(guard.loc(), Instruction{token.opcode(), immediate});
    }

    case TokenType::TableCopyInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      LocationGuard immediate_guard{tokenizer};
      At<CopyImmediate> immediate;
      if (context.features.reference_types_enabled()) {
        auto dst_var = ReadVarOpt(tokenizer, context);
        auto src_var = ReadVarOpt(tokenizer, context);
        immediate =
            MakeAt(immediate_guard.loc(), CopyImmediate{dst_var, src_var});
      } else {
        immediate = MakeAt(immediate_guard.loc(), CopyImmediate{});
      }
      return MakeAt(guard.loc(), Instruction{token.opcode(), immediate});
    }

    case TokenType::TableInitInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      LocationGuard immediate_guard{tokenizer};
      WASP_TRY_READ(segment_var, ReadVar(tokenizer, context));
      auto table_var_opt = ReadVarOpt(tokenizer, context);
      At<InitImmediate> immediate;
      if (table_var_opt) {
        // table.init $table $elem ; so vars need to be swapped.
        immediate = MakeAt(immediate_guard.loc(),
                           InitImmediate{*table_var_opt, segment_var});
      } else {
        // table.init $elem
        immediate =
            MakeAt(immediate_guard.loc(), InitImmediate{segment_var, nullopt});
      }
      return MakeAt(guard.loc(), Instruction{token.opcode(), immediate});
    }

    case TokenType::VarInstr:
    case TokenType::RefFuncInstr: {
      WASP_TRY(CheckOpcodeEnabled(token, context));
      tokenizer.Read();
      WASP_TRY_READ(var, ReadVar(tokenizer, context));
      return MakeAt(guard.loc(), Instruction{token.opcode(), var});
    }

    default:
      context.errors.OnError(
          token.loc, format("Expected plain instruction, got {}", token.type));
      return nullopt;
  }
}

auto ReadLabelOpt(Tokenizer& tokenizer, Context& context) -> OptAt<BindVar> {
  // Unlike ReadBindVarOpt, labels can be shadowed; don't check for duplicates.
  auto token_opt = tokenizer.Match(TokenType::Id);
  if (!token_opt) {
    context.label_names.NewUnbound();
    context.label_name_stack.push_back(nullopt);
    return nullopt;
  }

  BindVar bind_var{token_opt->as_string_view()};
  context.label_names.ReplaceBound(bind_var);
  context.label_name_stack.push_back(bind_var);
  return MakeAt(token_opt->loc, bind_var);
}

bool ReadEndLabelOpt(Tokenizer& tokenizer,
                     Context& context,
                     OptAt<BindVar> label) {
  NameMap dummy_name_map;
  auto end_label = ReadBindVarOpt(tokenizer, context, dummy_name_map);
  if (end_label) {
    if (!label) {
      context.errors.OnError(end_label->loc(),
                             format("Unexpected label {}", end_label));
      return false;
    } else if (*label != *end_label) {
      context.errors.OnError(
          end_label->loc(),
          format("Expected label {}, got {}", *label, *end_label));
      return false;
    }
  }
  return true;
}

auto ReadBlockImmediate(Tokenizer& tokenizer, Context& context)
    -> OptAt<BlockImmediate> {
  LocationGuard guard{tokenizer};
  auto label = ReadLabelOpt(tokenizer, context);

  // Don't use ReadFunctionTypeUse, since that always marks the type signature
  // as being used, even if it is an inline signature.
  auto type_use = ReadTypeUseOpt(tokenizer, context);
  WASP_TRY_READ(type, ReadFunctionType(tokenizer, context));
  auto ftu = FunctionTypeUse{type_use, type};
  if (!ftu.IsInlineType()) {
    context.function_type_map.Use(ftu);
  }
  return MakeAt(guard.loc(), BlockImmediate{label, ftu});
}

bool ReadOpcodeOpt(Tokenizer& tokenizer,
                   Context& context,
                   InstructionList& instructions,
                   TokenType token_type) {
  auto token_opt = tokenizer.Match(token_type);
  if (!token_opt) {
    return false;
  }
  instructions.push_back(
      MakeAt(token_opt->loc, Instruction{token_opt->opcode()}));
  return true;
}

bool ExpectOpcode(Tokenizer& tokenizer,
                  Context& context,
                  InstructionList& instructions,
                  TokenType token_type) {
  auto token = tokenizer.Peek();
  if (!ReadOpcodeOpt(tokenizer, context, instructions, token_type)) {
    context.errors.OnError(
        token.loc, format("Expected {}, got {}", token_type, token.type));
    return false;
  }
  return true;
}

bool ReadBlockInstruction(Tokenizer& tokenizer,
                          Context& context,
                          InstructionList& instructions) {
  LocationGuard guard{tokenizer};
  auto token_opt = tokenizer.Match(TokenType::BlockInstr);
  // Shouldn't be called when the TokenType is not a BlockInstr.
  assert(token_opt.has_value());

  WASP_TRY_READ(block, ReadBlockImmediate(tokenizer, context));
  instructions.push_back(
      MakeAt(guard.loc(), Instruction{token_opt->opcode(), block}));
  WASP_TRY(ReadInstructionList(tokenizer, context, instructions));

  switch (token_opt->opcode()) {
    case Opcode::If:
      if (ReadOpcodeOpt(tokenizer, context, instructions, TokenType::Else)) {
        WASP_TRY(ReadEndLabelOpt(tokenizer, context, block->label));
        WASP_TRY(ReadInstructionList(tokenizer, context, instructions));
      }
      break;

    case Opcode::Try:
      if (!context.features.exceptions_enabled()) {
        context.errors.OnError(token_opt->loc, "try instruction not allowed");
        return false;
      }
      WASP_TRY(
          ExpectOpcode(tokenizer, context, instructions, TokenType::Catch));
      WASP_TRY(ReadEndLabelOpt(tokenizer, context, block->label));
      WASP_TRY(ReadInstructionList(tokenizer, context, instructions));
      break;

    case Opcode::Block:
    case Opcode::Loop:
      break;

    default:
      WASP_UNREACHABLE();
  }

  WASP_TRY(ExpectOpcode(tokenizer, context, instructions, TokenType::End));
  WASP_TRY(ReadEndLabelOpt(tokenizer, context, block->label));
  context.EndBlock();
  return true;
}

bool ReadInstruction(Tokenizer& tokenizer,
                     Context& context,
                     InstructionList& instructions) {
  auto token = tokenizer.Peek();
  if (IsPlainInstruction(token)) {
    WASP_TRY_READ(instruction, ReadPlainInstruction(tokenizer, context));
    instructions.push_back(instruction);
  } else if (IsBlockInstruction(token)) {
    WASP_TRY(ReadBlockInstruction(tokenizer, context, instructions));
  } else if (IsExpression(tokenizer)) {
    WASP_TRY(ReadExpression(tokenizer, context, instructions));
  } else {
    context.errors.OnError(token.loc,
                           format("Expected instruction, got {}", token.type));
    return false;
  }
  return true;
}

bool ReadInstructionList(Tokenizer& tokenizer,
                         Context& context,
                         InstructionList& instructions) {
  while (IsInstruction(tokenizer)) {
    WASP_TRY(ReadInstruction(tokenizer, context, instructions));
  }
  return true;
}

bool ReadExpression(Tokenizer& tokenizer,
                    Context& context,
                    InstructionList& instructions) {
  WASP_TRY(Expect(tokenizer, context, TokenType::Lpar));

  auto token = tokenizer.Peek();

  if (IsPlainInstruction(token)) {
    WASP_TRY_READ(plain, ReadPlainInstruction(tokenizer, context));
    // Reorder the instructions, so `(A (B) (C))` becomes `(B) (C) (A)`.
    WASP_TRY(ReadExpressionList(tokenizer, context, instructions));
    instructions.push_back(plain);
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  } else if (IsBlockInstruction(token)) {
    LocationGuard guard{tokenizer};
    tokenizer.Read();
    WASP_TRY_READ(block, ReadBlockImmediate(tokenizer, context));
    auto block_instr = MakeAt(guard.loc(), Instruction{token.opcode(), block});

    switch (token.opcode()) {
      case Opcode::Block:
      case Opcode::Loop:
        instructions.push_back(block_instr);
        WASP_TRY(ReadInstructionList(tokenizer, context, instructions));
        break;

      case Opcode::If:
        // Read condition, if any. It doesn't need to exist, since the folded
        // `if` syntax is extremely flexible.
        WASP_TRY(ReadExpressionList(tokenizer, context, instructions));

        // The `if` instruction must come after the condition.
        instructions.push_back(block_instr);

        // Read then block.
        WASP_TRY(ExpectLpar(tokenizer, context, TokenType::Then));
        WASP_TRY(ReadInstructionList(tokenizer, context, instructions));
        WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));

        // Read else block, if any.
        if (tokenizer.Match(TokenType::Lpar)) {
          WASP_TRY(
              ExpectOpcode(tokenizer, context, instructions, TokenType::Else));
          WASP_TRY(ReadEndLabelOpt(tokenizer, context, block->label));
          WASP_TRY(ReadInstructionList(tokenizer, context, instructions));
          WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
        }
        break;

      case Opcode::Try:
        if (!context.features.exceptions_enabled()) {
          context.errors.OnError(token.loc, "try instruction not allowed");
          return false;
        }
        instructions.push_back(block_instr);
        WASP_TRY(ReadInstructionList(tokenizer, context, instructions));

        // Read catch block.
        WASP_TRY(Expect(tokenizer, context, TokenType::Lpar));
        WASP_TRY(
            ExpectOpcode(tokenizer, context, instructions, TokenType::Catch));
        WASP_TRY(ReadEndLabelOpt(tokenizer, context, block->label));
        WASP_TRY(ReadInstructionList(tokenizer, context, instructions));
        WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
        break;

      default:
        WASP_UNREACHABLE();
    }

    // Read final `)` and use its location as the `end` instruction.
    auto rpar = tokenizer.Peek();
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
    instructions.push_back(
        MakeAt(rpar.loc, Instruction{MakeAt(rpar.loc, Opcode::End)}));
    context.EndBlock();
  } else {
    context.errors.OnError(token.loc,
                           format("Expected expression, got {}", token.type));
    return false;
  }
  return true;
}

bool ReadExpressionList(Tokenizer& tokenizer,
                        Context& context,
                        InstructionList& instructions) {
  while (IsExpression(tokenizer)) {
    WASP_TRY(ReadExpression(tokenizer, context, instructions));
  }
  return true;
}

// Section 11: Data

auto ReadMemoryUseOpt(Tokenizer& tokenizer, Context& context) -> OptAt<Var> {
  return ReadVarUseOpt(tokenizer, context, TokenType::Memory);
}

auto ReadDataSegment(Tokenizer& tokenizer, Context& context)
    -> OptAt<DataSegment> {
  LocationGuard guard{tokenizer};
  WASP_TRY(ExpectLpar(tokenizer, context, TokenType::Data));

  if (context.features.bulk_memory_enabled()) {
    // LPAR DATA * bind_var_opt string_list RPAR
    // LPAR DATA * bind_var_opt memory_use offset string_list RPAR
    // LPAR DATA * bind_var_opt offset string_list RPAR  /* Sugar */
    auto name = ReadBindVarOpt(tokenizer, context, context.data_segment_names);
    auto memory_use_opt = ReadMemoryUseOpt(tokenizer, context);

    SegmentType segment_type;
    OptAt<ConstantExpression> offset_opt;
    if (memory_use_opt || tokenizer.Peek().type == TokenType::Lpar) {
      // LPAR DATA bind_var_opt memory_use * offset string_list RPAR
      // LPAR DATA bind_var_opt * offset string_list RPAR  /* Sugar */
      WASP_TRY_READ(offset, ReadOffsetExpression(tokenizer, context));
      offset_opt = offset;
      segment_type = SegmentType::Active;
    } else {
      // LPAR DATA bind_var_opt * string_list RPAR
      segment_type = SegmentType::Passive;
    }
    // ... * string_list RPAR
    WASP_TRY_READ(data, ReadTextList(tokenizer, context));
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));

    if (segment_type == SegmentType::Active) {
      assert(offset_opt.has_value());
      return MakeAt(guard.loc(),
                    DataSegment{name, memory_use_opt, *offset_opt, data});
    } else {
      return MakeAt(guard.loc(), DataSegment{name, data});
    }
  } else {
    // LPAR DATA var offset string_list RPAR
    // LPAR DATA offset string_list RPAR  /* Sugar */
    auto memory = ReadVarOpt(tokenizer, context);
    WASP_TRY_READ(offset, ReadOffsetExpression(tokenizer, context));
    WASP_TRY_READ(data, ReadTextList(tokenizer, context));
    WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
    return MakeAt(guard.loc(), DataSegment{nullopt, memory, offset, data});
  }
}

// Section 13: Event

auto ReadEventType(Tokenizer& tokenizer, Context& context) -> OptAt<EventType> {
  LocationGuard guard{tokenizer};
  auto attribute = EventAttribute::Exception;
  WASP_TRY_READ(type, ReadFunctionTypeUse(tokenizer, context));
  return MakeAt(guard.loc(), EventType{attribute, type});
}

auto ReadEvent(Tokenizer& tokenizer, Context& context) -> OptAt<Event> {
  LocationGuard guard{tokenizer};
  auto token = tokenizer.Peek();
  WASP_TRY(ExpectLpar(tokenizer, context, TokenType::Event));

  if (!context.features.exceptions_enabled()) {
    context.errors.OnError(token.loc, "Events not allowed");
    return nullopt;
  }

  auto name = ReadBindVarOpt(tokenizer, context, context.event_names);
  WASP_TRY_READ(exports, ReadInlineExportList(tokenizer, context));
  auto import_opt = ReadInlineImportOpt(tokenizer, context);
  context.seen_non_import |= !import_opt;

  WASP_TRY_READ(type, ReadEventType(tokenizer, context));
  WASP_TRY(Expect(tokenizer, context, TokenType::Rpar));
  return MakeAt(guard.loc(), Event{EventDesc{name, type}, import_opt, exports});
}

// Module

bool IsModuleItem(Tokenizer& tokenizer) {
  if (tokenizer.Peek().type != TokenType::Lpar) {
    return false;
  }

  auto token = tokenizer.Peek(1);
  return token.type == TokenType::Type || token.type == TokenType::Import ||
         token.type == TokenType::Func || token.type == TokenType::Table ||
         token.type == TokenType::Memory || token.type == TokenType::Global ||
         token.type == TokenType::Export || token.type == TokenType::Start ||
         token.type == TokenType::Elem || token.type == TokenType::Data ||
         token.type == TokenType::Event;
}

auto ReadModuleItem(Tokenizer& tokenizer, Context& context)
    -> OptAt<ModuleItem> {
  auto token = tokenizer.Peek();
  if (token.type != TokenType::Lpar) {
    context.errors.OnError(token.loc,
                           format("Expected '(', got {}", token.type));
    return nullopt;
  }

  token = tokenizer.Peek(1);
  switch (token.type) {
    case TokenType::Type: {
      WASP_TRY_READ(item, ReadTypeEntry(tokenizer, context));
      return MakeAt(item.loc(), ModuleItem{*item});
    }

    case TokenType::Import: {
      WASP_TRY_READ(item, ReadImport(tokenizer, context));
      return MakeAt(item.loc(), ModuleItem{*item});
    }

    case TokenType::Func: {
      WASP_TRY_READ(item, ReadFunction(tokenizer, context));
      return MakeAt(item.loc(), ModuleItem{*item});
    }

    case TokenType::Table: {
      WASP_TRY_READ(item, ReadTable(tokenizer, context));
      return MakeAt(item.loc(), ModuleItem{*item});
    }

    case TokenType::Memory: {
      WASP_TRY_READ(item, ReadMemory(tokenizer, context));
      return MakeAt(item.loc(), ModuleItem{*item});
    }

    case TokenType::Global: {
      WASP_TRY_READ(item, ReadGlobal(tokenizer, context));
      return MakeAt(item.loc(), ModuleItem{*item});
    }

    case TokenType::Export: {
      WASP_TRY_READ(item, ReadExport(tokenizer, context));
      return MakeAt(item.loc(), ModuleItem{*item});
    }

    case TokenType::Start: {
      WASP_TRY_READ(item, ReadStart(tokenizer, context));
      return MakeAt(item.loc(), ModuleItem{*item});
    }

    case TokenType::Elem: {
      WASP_TRY_READ(item, ReadElementSegment(tokenizer, context));
      return MakeAt(item.loc(), ModuleItem{*item});
    }

    case TokenType::Data: {
      WASP_TRY_READ(item, ReadDataSegment(tokenizer, context));
      return MakeAt(item.loc(), ModuleItem{*item});
    }

    case TokenType::Event: {
      WASP_TRY_READ(item, ReadEvent(tokenizer, context));
      return MakeAt(item.loc(), ModuleItem{*item});
    }

    default:
      context.errors.OnError(
          token.loc,
          format(
              "Expected 'type', 'import', 'func', 'table', 'memory', 'global', "
              "'export', 'start', 'elem', 'data', or 'event', got {}",
              token.type));
      return nullopt;
  }
}

auto ReadModule(Tokenizer& tokenizer, Context& context) -> optional<Module> {
  context.BeginModule();
  Module module;
  while (IsModuleItem(tokenizer)) {
    WASP_TRY_READ(item, ReadModuleItem(tokenizer, context));
    module.push_back(item);
  }

  auto deferred_types = context.EndModule();
  for (auto& type_entry : deferred_types) {
    module.push_back(ModuleItem{type_entry});
  }
  return module;
}

// Explicit instantiations.
template auto ReadInt<s8>(Tokenizer&, Context&) -> OptAt<s8>;
template auto ReadInt<u8>(Tokenizer&, Context&) -> OptAt<u8>;
template auto ReadInt<s16>(Tokenizer&, Context&) -> OptAt<s16>;
template auto ReadInt<u16>(Tokenizer&, Context&) -> OptAt<u16>;
template auto ReadInt<s32>(Tokenizer&, Context&) -> OptAt<s32>;
template auto ReadInt<u32>(Tokenizer&, Context&) -> OptAt<u32>;
template auto ReadInt<s64>(Tokenizer&, Context&) -> OptAt<s64>;
template auto ReadInt<u64>(Tokenizer&, Context&) -> OptAt<u64>;
template auto ReadFloat<f32>(Tokenizer&, Context&) -> OptAt<f32>;
template auto ReadFloat<f64>(Tokenizer&, Context&) -> OptAt<f64>;
template auto ReadSimdValues<s8, 16>(Tokenizer&, Context&) -> OptAt<v128>;
template auto ReadSimdValues<u8, 16>(Tokenizer&, Context&) -> OptAt<v128>;
template auto ReadSimdValues<s16, 8>(Tokenizer&, Context&) -> OptAt<v128>;
template auto ReadSimdValues<u16, 8>(Tokenizer&, Context&) -> OptAt<v128>;
template auto ReadSimdValues<s32, 4>(Tokenizer&, Context&) -> OptAt<v128>;
template auto ReadSimdValues<u32, 4>(Tokenizer&, Context&) -> OptAt<v128>;
template auto ReadSimdValues<s64, 2>(Tokenizer&, Context&) -> OptAt<v128>;
template auto ReadSimdValues<u64, 2>(Tokenizer&, Context&) -> OptAt<v128>;
template auto ReadSimdValues<f32, 4>(Tokenizer&, Context&) -> OptAt<v128>;
template auto ReadSimdValues<f64, 2>(Tokenizer&, Context&) -> OptAt<v128>;

}  // namespace text
}  // namespace wasp
