// Copyright 2021 The XLS Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "xls/dslx/bytecode/bytecode_emitter.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/statusor.h"
#include "xls/common/status/matchers.h"
#include "xls/dslx/bytecode/bytecode.h"
#include "xls/dslx/create_import_data.h"
#include "xls/dslx/frontend/ast.h"
#include "xls/dslx/import_data.h"
#include "xls/dslx/parse_and_typecheck.h"
#include "xls/dslx/type_system/parametric_env.h"

namespace xls::dslx {
namespace {

using status_testing::IsOkAndHolds;

absl::StatusOr<std::unique_ptr<BytecodeFunction>> EmitBytecodes(
    ImportData* import_data, std::string_view program,
    std::string_view fn_name) {
  XLS_ASSIGN_OR_RETURN(
      TypecheckedModule tm,
      ParseAndTypecheck(program, "test.x", "test", import_data));

  XLS_ASSIGN_OR_RETURN(TestFunction * tf, tm.module->GetTest(fn_name));

  return BytecodeEmitter::Emit(import_data, tm.type_info, tf->fn(),
                               std::nullopt);
}

// Verifies that a baseline translation - of a nearly-minimal test case -
// succeeds.
TEST(BytecodeEmitterTest, SimpleTranslation) {
  constexpr std::string_view kProgram = R"(fn one_plus_one() -> u32 {
  let foo = u32:1;
  foo + u32:2
})";

  auto import_data = CreateImportDataForTest();
  XLS_ASSERT_OK_AND_ASSIGN(
      TypecheckedModule tm,
      ParseAndTypecheck(kProgram, "test.x", "test", &import_data));

  XLS_ASSERT_OK_AND_ASSIGN(
      Function * f, tm.module->GetMemberOrError<Function>("one_plus_one"));
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      BytecodeEmitter::Emit(&import_data, tm.type_info, f, ParametricEnv()));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 5);
  const Bytecode* bc = bytecodes.data();
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);
  ASSERT_TRUE(bc->has_data());
  ASSERT_EQ(bc->value_data().value(), InterpValue::MakeU32(1));

  bc = &bytecodes[1];
  ASSERT_EQ(bc->op(), Bytecode::Op::kStore);
  ASSERT_TRUE(bc->has_data());
  XLS_ASSERT_OK_AND_ASSIGN(Bytecode::SlotIndex slot_index, bc->slot_index());
  ASSERT_EQ(slot_index.value(), 0);

  bc = &bytecodes[2];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLoad);
  ASSERT_TRUE(bc->has_data());
  XLS_ASSERT_OK_AND_ASSIGN(slot_index, bc->slot_index());
  ASSERT_EQ(slot_index.value(), 0);

  bc = &bytecodes[3];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);
  ASSERT_TRUE(bc->has_data());
  ASSERT_EQ(bc->value_data().value(), InterpValue::MakeU32(2));

  bc = &bytecodes[4];
  ASSERT_EQ(bc->op(), Bytecode::Op::kAdd);
  ASSERT_FALSE(bc->has_data());
}

// Validates emission of AssertEq builtins.
TEST(BytecodeEmitterTest, AssertEq) {
  constexpr std::string_view kProgram = R"(#[test]
fn expect_fail() -> u32{
  let foo = u32:3;
  assert_eq(foo, u32:2);
  foo
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "expect_fail"));

  EXPECT_EQ(BytecodesToString(bf->bytecodes(), /*source_locs=*/false),
            R"(000 literal u32:3
001 store 0
002 load 0
003 literal u32:2
004 literal builtin:assert_eq
005 call assert_eq(foo, u32:2) : {}
006 pop
007 load 0)");
}

// Validates emission of Let nodes with structured bindings.
TEST(BytecodeEmitterTest, DestructuringLet) {
  constexpr std::string_view kProgram = R"(#[test]
fn has_name_def_tree() -> (u32, u64, uN[128]) {
  let (a, b, (c, d)) = (u4:0, u8:1, (u16:2, (u32:3, u64:4, uN[128]:5)));
  assert_eq(a, u4:0);
  assert_eq(b, u8:1);
  assert_eq(c, u16:2);
  assert_eq(d, (u32:3, u64:4, uN[128]:5));
  d
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "has_name_def_tree"));

  EXPECT_EQ(BytecodesToString(bf->bytecodes(), /*source_locs=*/false),
            R"(000 literal u4:0
001 literal u8:1
002 literal u16:2
003 literal u32:3
004 literal u64:4
005 literal u128:0x5
006 create_tuple 3
007 create_tuple 2
008 create_tuple 3
009 expand_tuple
010 store 0
011 store 1
012 expand_tuple
013 store 2
014 store 3
015 load 0
016 literal u4:0
017 literal builtin:assert_eq
018 call assert_eq(a, u4:0) : {}
019 pop
020 load 1
021 literal u8:1
022 literal builtin:assert_eq
023 call assert_eq(b, u8:1) : {}
024 pop
025 load 2
026 literal u16:2
027 literal builtin:assert_eq
028 call assert_eq(c, u16:2) : {}
029 pop
030 load 3
031 literal u32:3
032 literal u64:4
033 literal u128:0x5
034 create_tuple 3
035 literal builtin:assert_eq
036 call assert_eq(d, (u32:3, u64:4, uN[128]:5)) : {}
037 pop
038 load 3)");
}

TEST(BytecodeEmitterTest, Ternary) {
  constexpr std::string_view kProgram = R"(#[test]
fn do_ternary() -> u32 {
  if true { u32:42 } else { u32:64 }
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BytecodeFunction> bf,
                           EmitBytecodes(&import_data, kProgram, "do_ternary"));

  EXPECT_EQ(BytecodesToString(bf->bytecodes(), /*source_locs=*/false),
            R"(000 literal u1:1
001 jump_rel_if +3
002 literal u32:64
003 jump_rel +3
004 jump_dest
005 literal u32:42
006 jump_dest)");
}

TEST(BytecodeEmitterTest, Shadowing) {
  constexpr std::string_view kProgram = R"(#[test]
fn f() -> u32 {
  let x = u32:42;
  let x = u32:64;
  x
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BytecodeFunction> bf,
                           EmitBytecodes(&import_data, kProgram, "f"));

  EXPECT_EQ(BytecodesToString(bf->bytecodes(), /*source_locs=*/false),
            R"(000 literal u32:42
001 store 0
002 literal u32:64
003 store 1
004 load 1)");
}

TEST(BytecodeEmitterTest, MatchSimpleArms) {
  constexpr std::string_view kProgram = R"(#[test]
fn do_match() -> u32 {
  let x = u32:77;
  match x {
    u32:42 => u32:64,
    u32:64 => u32:42,
    _ => x + u32:1
  }
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BytecodeFunction> bf,
                           EmitBytecodes(&import_data, kProgram, "do_match"));

  EXPECT_EQ(BytecodesToString(bf->bytecodes(), /*source_locs=*/false),
            R"(000 literal u32:77
001 store 0
002 load 0
003 dup
004 match_arm value:u32:42
005 invert
006 jump_rel_if +4
007 pop
008 literal u32:64
009 jump_rel +21
010 jump_dest
011 dup
012 match_arm value:u32:64
013 invert
014 jump_rel_if +4
015 pop
016 literal u32:42
017 jump_rel +13
018 jump_dest
019 dup
020 match_arm wildcard
021 invert
022 jump_rel_if +6
023 pop
024 load 0
025 literal u32:1
026 add
027 jump_rel +3
028 jump_dest
029 fail trace data: The value was not matched: value: , default
030 jump_dest)");
}

TEST(BytecodeEmitterTest, BytecodesFromString) {
  std::string s = R"(000 literal u2:1
001 literal s2:-1
002 literal s2:-2
003 literal s3:-1
004 literal u32:42)";
  XLS_ASSERT_OK_AND_ASSIGN(std::vector<Bytecode> bytecodes,
                           BytecodesFromString(s));
  EXPECT_THAT(bytecodes.at(3).value_data(),
              IsOkAndHolds(InterpValue::MakeSBits(3, -1)));
  EXPECT_EQ(BytecodesToString(bytecodes, /*source_locs=*/false), s);
}

// Tests emission of all of the supported binary operators.
TEST(BytecodeEmitterTest, Binops) {
  constexpr std::string_view kProgram = R"(#[test]
fn binops_galore() {
  let a = u32:4;
  let b = u32:2;

  let add = a + b;
  let and = a & b;
  let concat = a ++ b;
  let div = a / b;
  let eq = a == b;
  let ge = a >= b;
  let gt = a > b;
  let le = a <= b;
  let lt = a < b;
  let mul = a * b;
  let ne = a != b;
  let or = a | b;
  let shl = a << b;
  let shr = a >> b;
  let sub = a - b;
  let xor = a ^ b;

  ()
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "binops_galore"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 69);
  const Bytecode* bc = &bytecodes[6];
  ASSERT_EQ(bc->op(), Bytecode::Op::kAdd);

  bc = &bytecodes[10];
  ASSERT_EQ(bc->op(), Bytecode::Op::kAnd);

  bc = &bytecodes[14];
  ASSERT_EQ(bc->op(), Bytecode::Op::kConcat);

  bc = &bytecodes[18];
  ASSERT_EQ(bc->op(), Bytecode::Op::kDiv);

  bc = &bytecodes[22];
  ASSERT_EQ(bc->op(), Bytecode::Op::kEq);

  bc = &bytecodes[26];
  ASSERT_EQ(bc->op(), Bytecode::Op::kGe);

  bc = &bytecodes[30];
  ASSERT_EQ(bc->op(), Bytecode::Op::kGt);

  bc = &bytecodes[34];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLe);

  bc = &bytecodes[38];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLt);

  bc = &bytecodes[42];
  ASSERT_EQ(bc->op(), Bytecode::Op::kMul);

  bc = &bytecodes[46];
  ASSERT_EQ(bc->op(), Bytecode::Op::kNe);

  bc = &bytecodes[50];
  ASSERT_EQ(bc->op(), Bytecode::Op::kOr);

  bc = &bytecodes[54];
  ASSERT_EQ(bc->op(), Bytecode::Op::kShl);

  bc = &bytecodes[58];
  ASSERT_EQ(bc->op(), Bytecode::Op::kShr);

  bc = &bytecodes[62];
  ASSERT_EQ(bc->op(), Bytecode::Op::kSub);

  bc = &bytecodes[66];
  ASSERT_EQ(bc->op(), Bytecode::Op::kXor);
}

// Tests emission of all of the supported binary operators.
TEST(BytecodeEmitterTest, Unops) {
  constexpr std::string_view kProgram = R"(#[test]
fn unops() {
  let a = s32:32;
  let b = !a;
  let c = -b;
  ()
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BytecodeFunction> bf,
                           EmitBytecodes(&import_data, kProgram, "unops"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 9);
  const Bytecode* bc = &bytecodes[3];
  ASSERT_EQ(bc->op(), Bytecode::Op::kInvert);

  bc = &bytecodes[6];
  ASSERT_EQ(bc->op(), Bytecode::Op::kNegate);
}

// Tests array creation.
TEST(BytecodeEmitterTest, Arrays) {
  constexpr std::string_view kProgram = R"(#[test]
fn arrays() -> u32[3] {
  let a = u32:32;
  u32[3]:[u32:0, u32:1, a]
}
)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BytecodeFunction> bf,
                           EmitBytecodes(&import_data, kProgram, "arrays"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 6);
  const Bytecode* bc = &bytecodes[5];
  ASSERT_EQ(bc->op(), Bytecode::Op::kCreateArray);
  ASSERT_TRUE(bc->has_data());
  XLS_ASSERT_OK_AND_ASSIGN(Bytecode::NumElements num_elements,
                           bc->num_elements());
  ASSERT_EQ(num_elements.value(), 3);
}

// Tests large constexpr 2D array creation doesn't create a skillion bytecodes.
TEST(BytecodeEmitterTest, TwoDimensionalArrayLiteral) {
  constexpr std::string_view kProgram = R"(#[test]
fn make_2d_array() -> u32[1024][1024] {
  const A: u32[1024][1024] = u32[1024][1024]:[u32[1024]:[0, ...], ...];
  A
}
)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "make_2d_array"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 3);
}

// Tests emission of kIndex ops on arrays.
TEST(BytecodeEmitterTest, IndexArray) {
  constexpr std::string_view kProgram = R"(#[test]
fn index_array() -> u32 {
  let a = u32[3]:[0, 1, 2];
  let b = bits[32][3]:[3, 4, 5];

  a[u32:0] + b[u32:1]
}
)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "index_array"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 11);

  const std::string_view kWant =
      R"(literal [u32:0, u32:1, u32:2] @ test.x:3:18-3:27
store 0 @ test.x:3:7-3:8
literal [u32:3, u32:4, u32:5] @ test.x:4:23-4:32
store 1 @ test.x:4:7-4:8
load 0 @ test.x:6:3-6:4
literal u32:0 @ test.x:6:9-6:10
index @ test.x:6:4-6:11
load 1 @ test.x:6:14-6:15
literal u32:1 @ test.x:6:20-6:21
index @ test.x:6:15-6:22
add @ test.x:6:12-6:13)";
  std::string got = absl::StrJoin(bf->bytecodes(), "\n",
                                  [](std::string* out, const Bytecode& b) {
                                    absl::StrAppend(out, b.ToString());
                                  });

  EXPECT_EQ(kWant, got);
}

// Tests emission of kIndex ops on tuples.
TEST(BytecodeEmitterTest, IndexTuple) {
  constexpr std::string_view kProgram = R"(#[test]
fn index_tuple() -> u32 {
  let a = (u16:0, u32:1, u64:2);
  let b = (bits[128]:3, bits[32]:4);

  a.1 + b.1
}
)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "index_tuple"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 16);
  const Bytecode* bc = &bytecodes[11];
  ASSERT_EQ(bc->op(), Bytecode::Op::kIndex);

  bc = &bytecodes[14];
  ASSERT_EQ(bc->op(), Bytecode::Op::kIndex);
}

// Tests a regular a[x:y] slice op.
TEST(BytecodeEmitterTest, SimpleSlice) {
  constexpr std::string_view kProgram = R"(#[test]
fn simple_slice() -> u16 {
  let a = u32:0xdeadbeef;
  a[16:32]
}
)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "simple_slice"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 6);
  const Bytecode* bc = &bytecodes[3];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);

  bc = &bytecodes[4];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);

  bc = &bytecodes[5];
  ASSERT_EQ(bc->op(), Bytecode::Op::kSlice);
}

// Tests a slice from the start: a[-x:].
TEST(BytecodeEmitterTest, NegativeStartSlice) {
  constexpr std::string_view kProgram = R"(#[test]
fn negative_start_slice() -> u16 {
  let a = u32:0xdeadbeef;
  a[-16:]
}
)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "negative_start_slice"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 6);
  const Bytecode* bc = &bytecodes[3];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);

  bc = &bytecodes[4];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);

  bc = &bytecodes[5];
  ASSERT_EQ(bc->op(), Bytecode::Op::kSlice);
}

// Tests a slice from the end: a[:-x].
TEST(BytecodeEmitterTest, NegativeEndSlice) {
  constexpr std::string_view kProgram = R"(#[test]
fn negative_end_slice() -> u16 {
  let a = u32:0xdeadbeef;
  a[:-16]
}
)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "negative_end_slice"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 6);
  const Bytecode* bc = &bytecodes[3];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);

  bc = &bytecodes[4];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);

  bc = &bytecodes[5];
  ASSERT_EQ(bc->op(), Bytecode::Op::kSlice);
}

// Tests a slice from both ends: a[-x:-y].
TEST(BytecodeEmitterTest, BothNegativeSlice) {
  constexpr std::string_view kProgram = R"(#[test]
fn both_negative_slice() -> u8 {
  let a = u32:0xdeadbeef;
  a[-16:-8]
}
)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "both_negative_slice"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 6);
  const Bytecode* bc = &bytecodes[3];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);

  bc = &bytecodes[4];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);

  bc = &bytecodes[5];
  ASSERT_EQ(bc->op(), Bytecode::Op::kSlice);
}

// Tests the width slice op.
TEST(BytecodeEmitterTest, WidthSlice) {
  constexpr std::string_view kProgram = R"(#[test]
fn width_slice() -> u16 {
  let a = u32:0xdeadbeef;
  a[u32:8 +: bits[16]]
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "width_slice"));
  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 5);

  const Bytecode* bc = &bytecodes[3];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);

  bc = &bytecodes[4];
  ASSERT_EQ(bc->op(), Bytecode::Op::kWidthSlice);
}

TEST(BytecodeEmitterTest, LocalEnumRef) {
  constexpr std::string_view kProgram = R"(enum MyEnum : u23 {
  VAL_0 = 0,
  VAL_1 = 1,
  VAL_2 = 2,
}

#[test]
fn local_enum_ref() -> MyEnum {
  MyEnum::VAL_1
}
)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "local_enum_ref"));
  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 1);

  const Bytecode* bc = bytecodes.data();
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);
  ASSERT_TRUE(bc->has_data());
  EXPECT_THAT(bytecodes.at(0).value_data(),
              IsOkAndHolds(InterpValue::MakeSBits(23, 1)));
}

TEST(BytecodeEmitterTest, ImportedEnumRef) {
  constexpr std::string_view kImportedProgram = R"(pub enum ImportedEnum : u4 {
  VAL_0 = 0,
  VAL_1 = 1,
  VAL_2 = 2,
  VAL_3 = 3,
}
)";
  constexpr std::string_view kBaseProgram = R"(
import import_0

#[test]
fn imported_enum_ref() -> import_0::ImportedEnum {
  import_0::ImportedEnum::VAL_2
}
)";

  auto import_data = CreateImportDataForTest();
  XLS_ASSERT_OK_AND_ASSIGN(TypecheckedModule tm,
                           ParseAndTypecheck(kImportedProgram, "import_0.x",
                                             "import_0", &import_data));

  XLS_ASSERT_OK_AND_ASSIGN(
      tm, ParseAndTypecheck(kBaseProgram, "test.x", "test", &import_data));

  XLS_ASSERT_OK_AND_ASSIGN(TestFunction * tf,
                           tm.module->GetTest("imported_enum_ref"));
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BytecodeFunction> bf,
                           BytecodeEmitter::Emit(&import_data, tm.type_info,
                                                 tf->fn(), ParametricEnv()));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 1);

  const Bytecode* bc = bytecodes.data();
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);
  ASSERT_TRUE(bc->has_data());
  EXPECT_THAT(bytecodes.at(0).value_data(),
              IsOkAndHolds(InterpValue::MakeSBits(4, 2)));
}

TEST(BytecodeEmitterTest, ImportedConstant) {
  constexpr std::string_view kImportedProgram =
      R"(pub const MY_CONST = u3:2;)";
  constexpr std::string_view kBaseProgram = R"(
import import_0

#[test]
fn imported_enum_ref() -> u3 {
  import_0::MY_CONST
}
)";

  auto import_data = CreateImportDataForTest();
  XLS_ASSERT_OK_AND_ASSIGN(TypecheckedModule tm,
                           ParseAndTypecheck(kImportedProgram, "import_0.x",
                                             "import_0", &import_data));

  XLS_ASSERT_OK_AND_ASSIGN(
      tm, ParseAndTypecheck(kBaseProgram, "test.x", "test", &import_data));

  XLS_ASSERT_OK_AND_ASSIGN(TestFunction * tf,
                           tm.module->GetTest("imported_enum_ref"));
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BytecodeFunction> bf,
                           BytecodeEmitter::Emit(&import_data, tm.type_info,
                                                 tf->fn(), ParametricEnv()));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 1);

  const Bytecode* bc = bytecodes.data();
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);
  ASSERT_TRUE(bc->has_data());
  EXPECT_THAT(bytecodes.at(0).value_data(),
              IsOkAndHolds(InterpValue::MakeSBits(3, 2)));
}

TEST(BytecodeEmitterTest, HandlesConstRefs) {
  constexpr std::string_view kProgram = R"(const kFoo = u32:100;

#[test]
fn handles_const_refs() -> u32 {
  let a = u32:200;
  a + kFoo
}
)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "handles_const_refs"));
  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 5);
  const Bytecode* bc = &bytecodes[3];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);
  XLS_ASSERT_OK_AND_ASSIGN(InterpValue value, bc->value_data());
  XLS_ASSERT_OK_AND_ASSIGN(int64_t int_value, value.GetBitValueInt64());
  ASSERT_EQ(int_value, 100);
}

TEST(BytecodeEmitterTest, HandlesStructInstances) {
  constexpr std::string_view kProgram = R"(struct MyStruct {
  x: u32,
  y: u64,
}

#[test]
fn handles_struct_instances() -> MyStruct {
  let x = u32:2;
  MyStruct { x: x, y: u64:3 }
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "handles_struct_instances"));
  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 5);
  const Bytecode* bc = &bytecodes[4];
  ASSERT_EQ(bc->op(), Bytecode::Op::kCreateTuple);
}

TEST(BytecodeEmitterTest, HandlesAttr) {
  constexpr std::string_view kProgram = R"(struct MyStruct {
  x: u32,
  y: u64,
}

#[test]
fn handles_attr() -> u64 {
  MyStruct { x: u32:0, y: u64:0xbeef }.y
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "handles_attr"));
  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 5);
  const Bytecode* bc = &bytecodes[4];
  ASSERT_EQ(bc->op(), Bytecode::Op::kTupleIndex);
}

TEST(BytecodeEmitterTest, CastBitsToBits) {
  constexpr std::string_view kProgram = R"(#[test]
fn cast_bits_to_bits() -> u64 {
  let a = s16:-4;
  a as u64
}
)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "cast_bits_to_bits"));
  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 4);
  const Bytecode* bc = &bytecodes[3];
  ASSERT_EQ(bc->op(), Bytecode::Op::kCast);
}

TEST(BytecodeEmitterTest, CastArrayToBits) {
  constexpr std::string_view kProgram = R"(#[test]
fn cast_array_to_bits() -> u32 {
  let a = u8[4]:[0xc, 0xa, 0xf, 0xe];
  a as u32
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "cast_array_to_bits"));
  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 4);

  const std::string_view kWant =
      R"(literal [u8:12, u8:10, u8:15, u8:14] @ test.x:3:17-3:37
store 0 @ test.x:3:7-3:8
load 0 @ test.x:4:3-4:4
cast uN[32] @ test.x:4:3-4:11)";
  std::string got = absl::StrJoin(bf->bytecodes(), "\n",
                                  [](std::string* out, const Bytecode& b) {
                                    absl::StrAppend(out, b.ToString());
                                  });

  EXPECT_EQ(kWant, got);
}

TEST(BytecodeEmitterTest, CastBitsToArray) {
  constexpr std::string_view kProgram = R"(#[test]
fn cast_bits_to_array() -> u8 {
  let a = u32:0x0c0a0f0e;
  let b = a as u8[4];
  b[u32:2]
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "cast_bits_to_array"));
  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 8);
  const Bytecode* bc = &bytecodes[3];
  ASSERT_EQ(bc->op(), Bytecode::Op::kCast);
}

TEST(BytecodeEmitterTest, CastEnumToBits) {
  constexpr std::string_view kProgram = R"(enum MyEnum : u3 {
  VAL_0 = 0,
  VAL_1 = 1,
  VAL_2 = 2,
  VAL_3 = 3,
}

#[test]
fn cast_enum_to_bits() -> u3 {
  let a = MyEnum::VAL_3;
  a as u3
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "cast_enum_to_bits"));
  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 4);
  const Bytecode* bc = &bytecodes[3];
  ASSERT_EQ(bc->op(), Bytecode::Op::kCast);
}

TEST(BytecodeEmitterTest, CastBitsToEnum) {
  constexpr std::string_view kProgram = R"(enum MyEnum : u3 {
  VAL_0 = 0,
  VAL_1 = 1,
  VAL_2 = 2,
  VAL_3 = 3,
}

#[test]
fn cast_bits_to_enum() -> MyEnum {
  let a = u3:2;
  a as MyEnum
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "cast_bits_to_enum"));
  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 4);
  const Bytecode* bc = &bytecodes[3];
  ASSERT_EQ(bc->op(), Bytecode::Op::kCast);
}

TEST(BytecodeEmitterTest, HandlesSplatStructInstances) {
  constexpr std::string_view kProgram = R"(struct MyStruct {
  x: u16,
  y: u32,
  z: u64,
}

#[test]
fn handles_struct_instances() -> MyStruct {
  let a = u16:2;
  let b = MyStruct { z: u64:0xbeef, x: a, y: u32:3 };
  MyStruct { y:u32:0xf00d, ..b }
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      EmitBytecodes(&import_data, kProgram, "handles_struct_instances"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  const Bytecode* bc = &bytecodes[7];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLoad);
  bc = &bytecodes[8];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);
  bc = &bytecodes[9];
  ASSERT_EQ(bc->op(), Bytecode::Op::kIndex);

  bc = &bytecodes[10];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);

  bc = &bytecodes[11];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLoad);
  bc = &bytecodes[12];
  ASSERT_EQ(bc->op(), Bytecode::Op::kLiteral);
  bc = &bytecodes[13];
  ASSERT_EQ(bc->op(), Bytecode::Op::kIndex);
}

TEST(BytecodeEmitterTest, Params) {
  constexpr std::string_view kProgram = R"(
fn has_params(x: u32, y: u64) -> u48 {
  let a = u48:100;
  let x = x as u48 + a;
  let y = x + y as u48;
  x + y
})";

  auto import_data = CreateImportDataForTest();
  XLS_ASSERT_OK_AND_ASSIGN(
      TypecheckedModule tm,
      ParseAndTypecheck(kProgram, "test.x", "test", &import_data));
  XLS_ASSERT_OK_AND_ASSIGN(Function * f,
                           tm.module->GetMemberOrError<Function>("has_params"));
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      BytecodeEmitter::Emit(&import_data, tm.type_info, f, ParametricEnv()));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 15);

  const Bytecode* bc = &bytecodes[2];
  EXPECT_EQ(bc->op(), Bytecode::Op::kLoad);
  ASSERT_TRUE(bc->has_data());
  XLS_ASSERT_OK_AND_ASSIGN(Bytecode::SlotIndex slot_index, bc->slot_index());
  ASSERT_EQ(slot_index.value(), 0);

  bc = &bytecodes[7];
  EXPECT_EQ(bc->op(), Bytecode::Op::kLoad);
  ASSERT_TRUE(bc->has_data());
  XLS_ASSERT_OK_AND_ASSIGN(slot_index, bc->slot_index());
  ASSERT_EQ(slot_index.value(), 3);

  bc = &bytecodes[8];
  EXPECT_EQ(bc->op(), Bytecode::Op::kLoad);
  ASSERT_TRUE(bc->has_data());
  XLS_ASSERT_OK_AND_ASSIGN(slot_index, bc->slot_index());
  ASSERT_EQ(slot_index.value(), 1);

  bc = &bytecodes[12];
  EXPECT_EQ(bc->op(), Bytecode::Op::kLoad);
  ASSERT_TRUE(bc->has_data());
  XLS_ASSERT_OK_AND_ASSIGN(slot_index, bc->slot_index());
  ASSERT_EQ(slot_index.value(), 3);

  bc = &bytecodes[13];
  EXPECT_EQ(bc->op(), Bytecode::Op::kLoad);
  ASSERT_TRUE(bc->has_data());
  XLS_ASSERT_OK_AND_ASSIGN(slot_index, bc->slot_index());
  ASSERT_EQ(slot_index.value(), 4);
}

TEST(BytecodeEmitterTest, Strings) {
  constexpr std::string_view kProgram = R"(
#[test]
fn main() -> u8[13] {
  "tofu sandwich"
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BytecodeFunction> bf,
                           EmitBytecodes(&import_data, kProgram, "main"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 1);
  const Bytecode* bc = bytecodes.data();
  XLS_ASSERT_OK_AND_ASSIGN(InterpValue value, bc->value_data());
  XLS_ASSERT_OK_AND_ASSIGN(int64_t length, value.GetLength());
  EXPECT_EQ(13, length);
  XLS_ASSERT_OK_AND_ASSIGN(uint64_t char_value,
                           value.GetValuesOrDie().at(0).GetBitValueUint64());
  EXPECT_EQ(char_value, 't');
}

TEST(BytecodeEmitterTest, SimpleParametric) {
  constexpr std::string_view kProgram = R"(
fn foo<N: u32>(x: uN[N]) -> uN[N] {
  x * x
}

#[test]
fn main() -> u32 {
  let a = foo<u32:16>(u16:4);
  let b = foo(u32:8);
  a as u32 + b
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BytecodeFunction> bf,
                           EmitBytecodes(&import_data, kProgram, "main"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 12);
  const Bytecode* bc = &bytecodes[2];
  EXPECT_EQ(bc->op(), Bytecode::Op::kCall);
  XLS_ASSERT_OK_AND_ASSIGN(Bytecode::InvocationData id, bc->invocation_data());
  NameRef* name_ref = dynamic_cast<NameRef*>(id.invocation->callee());
  ASSERT_NE(name_ref, nullptr);
  EXPECT_EQ(name_ref->identifier(), "foo");

  bc = &bytecodes[6];
  EXPECT_EQ(bc->op(), Bytecode::Op::kCall);
  XLS_ASSERT_OK_AND_ASSIGN(id, bc->invocation_data());
  name_ref = dynamic_cast<NameRef*>(id.invocation->callee());
  ASSERT_NE(name_ref, nullptr);
  EXPECT_EQ(name_ref->identifier(), "foo");
}

TEST(BytecodeEmitterTest, SimpleFor) {
  constexpr std::string_view kProgram = R"(#[test]
fn main() -> u32 {
  for (i, accum) : (u32, u32) in range(u32:0, u32:8) {
    accum + i
  }(u32:1)
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BytecodeFunction> bf,
                           EmitBytecodes(&import_data, kProgram, "main"));

  // Since `for` generates a complex set of bytecodes, we test. every. one.
  // To make that a bit easier, we do string comparison.
  const std::vector<std::string> kExpected = {
      "literal u32:0 @ test.x:3:44-3:45",
      "literal u32:8 @ test.x:3:51-3:52",
      "literal builtin:range @ test.x:3:34-3:39",
      "call range(u32:0, u32:8) : {} @ test.x:3:39-3:53",
      "store 0 @ test.x:3:6-5:11",
      "literal u32:0 @ test.x:3:6-5:11",
      "store 1 @ test.x:3:6-5:11",
      "literal u32:1 @ test.x:5:9-5:10",
      "jump_dest @ test.x:3:6-5:11",
      "load 1 @ test.x:3:6-5:11",
      "literal u32:8 @ test.x:3:6-5:11",
      "eq @ test.x:3:6-5:11",
      "jump_rel_if +17 @ test.x:3:6-5:11",
      "load 0 @ test.x:3:6-5:11",
      "load 1 @ test.x:3:6-5:11",
      "index @ test.x:3:6-5:11",
      "swap @ test.x:3:6-5:11",
      "create_tuple 2 @ test.x:3:6-5:11",
      "expand_tuple @ test.x:3:7-3:17",
      "store 2 @ test.x:3:8-3:9",
      "store 3 @ test.x:3:11-3:16",
      "load 3 @ test.x:4:5-4:10",
      "load 2 @ test.x:4:13-4:14",
      "add @ test.x:4:11-4:12",
      "load 1 @ test.x:3:6-5:11",
      "literal u32:1 @ test.x:3:6-5:11",
      "add @ test.x:3:6-5:11",
      "store 1 @ test.x:3:6-5:11",
      "jump_rel -20 @ test.x:3:6-5:11",
      "jump_dest @ test.x:3:6-5:11",
  };

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 30);
  for (int i = 0; i < bytecodes.size(); i++) {
    ASSERT_EQ(bytecodes[i].ToString(), kExpected[i]);
  }
}

TEST(BytecodeEmitterTest, ForWithCover) {
  constexpr std::string_view kProgram = R"(
struct SomeStruct {
  some_bool: bool
}

#[test]
fn test_main(s: SomeStruct) {
  for  (_, ()) in u32:0..u32:4 {
    let _ = cover!("whee", s.some_bool);
    ()
  }(())
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BytecodeFunction> bf,
                           EmitBytecodes(&import_data, kProgram, "test_main"));

  const std::string_view kWant = R"(literal u32:0 @ test.x:8:23-8:24
literal u32:4 @ test.x:8:30-8:31
range @ test.x:8:23-8:31
store 1 @ test.x:8:6-11:8
literal u32:0 @ test.x:8:6-11:8
store 2 @ test.x:8:6-11:8
create_tuple 0 @ test.x:11:5-11:7
jump_dest @ test.x:8:6-11:8
load 2 @ test.x:8:6-11:8
literal u32:4 @ test.x:8:6-11:8
eq @ test.x:8:6-11:8
jump_rel_if +22 @ test.x:8:6-11:8
load 1 @ test.x:8:6-11:8
load 2 @ test.x:8:6-11:8
index @ test.x:8:6-11:8
swap @ test.x:8:6-11:8
create_tuple 2 @ test.x:8:6-11:8
expand_tuple @ test.x:8:8-8:15
pop @ test.x:8:9-8:10
expand_tuple @ test.x:8:12-8:14
literal [u8:119, u8:104, u8:101, u8:101] @ test.x:9:20-9:26
load 0 @ test.x:9:28-9:29
literal u64:0 @ test.x:9:29-9:39
tuple_index @ test.x:9:29-9:39
literal builtin:cover! @ test.x:9:13-9:19
call cover!("whee", s.some_bool) : {} @ test.x:9:19-9:40
pop @ test.x:9:9-9:10
create_tuple 0 @ test.x:10:5-10:7
load 2 @ test.x:8:6-11:8
literal u32:1 @ test.x:8:6-11:8
add @ test.x:8:6-11:8
store 2 @ test.x:8:6-11:8
jump_rel -25 @ test.x:8:6-11:8
jump_dest @ test.x:8:6-11:8)";
  std::string got = absl::StrJoin(bf->bytecodes(), "\n",
                                  [](std::string* out, const Bytecode& b) {
                                    absl::StrAppend(out, b.ToString());
                                  });

  EXPECT_EQ(kWant, got);
}

TEST(BytecodeEmitterTest, Range) {
  constexpr std::string_view kProgram = R"(#[test]
fn main() -> u32[8] {
  let x = u32:8;
  let y = u32:16;
  x..y
})";
  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BytecodeFunction> bf,
                           EmitBytecodes(&import_data, kProgram, "main"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 7);
  const Bytecode* bc = &bytecodes[6];
  ASSERT_EQ(bc->op(), Bytecode::Op::kRange);
}

TEST(BytecodeEmitterTest, ShlAndShr) {
  constexpr std::string_view kProgram = R"(#[test]
fn main() -> u32 {
  let x = u32:8;
  let y = u32:16;
  x << y >> y
})";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BytecodeFunction> bf,
                           EmitBytecodes(&import_data, kProgram, "main"));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 9);
  const Bytecode* bc = &bytecodes[6];
  ASSERT_EQ(bc->op(), Bytecode::Op::kShl);

  bc = &bytecodes[8];
  ASSERT_EQ(bc->op(), Bytecode::Op::kShr);
}

TEST(BytecodeEmitterTest, ParameterizedTypeDefToImportedEnum) {
  constexpr std::string_view kImported = R"(
pub struct ImportedStruct<X: u32> {
  x: uN[X],
}

pub enum ImportedEnum : u32 {
  EAT = 0,
  YOUR = 1,
  VEGGIES = 2
})";

  constexpr std::string_view kProgram = R"(
import imported

type MyEnum = imported::ImportedEnum;
type MyStruct = imported::ImportedStruct<16>;

#[test]
fn main() -> u32 {
  let foo = MyStruct { x: u16:100 };
  foo.x as u32 + (MyEnum::VEGGIES as u32)
}

)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      TypecheckedModule tm,
      ParseAndTypecheck(kImported, "imported.x", "imported", &import_data));

  XLS_ASSERT_OK_AND_ASSIGN(
      tm, ParseAndTypecheck(kProgram, "test.x", "test", &import_data));

  XLS_ASSERT_OK_AND_ASSIGN(TestFunction * tf, tm.module->GetTest("main"));

  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BytecodeFunction> bf,
                           BytecodeEmitter::Emit(&import_data, tm.type_info,
                                                 tf->fn(), ParametricEnv()));

  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 10);
}

TEST(BytecodeEmitterTest, BasicProc) {
  // We can only test 0-arg procs (both config and next), since procs are only
  // typechecked if spawned by a top-level (i.e., 0-arg) proc.
  constexpr std::string_view kProgram = R"(
proc Foo {
  x: chan<u32> in;
  y: u32;
  init { () }
  config() {
    let (p, c) = chan<u32>;
    (c, u32:100)
  }

  next(tok: token, state: ()) {
    ()
  }
}
)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      TypecheckedModule tm,
      ParseAndTypecheck(kProgram, "test.x", "test", &import_data));
  XLS_ASSERT_OK_AND_ASSIGN(Proc * p, tm.module->GetMemberOrError<Proc>("Foo"));
  XLS_ASSERT_OK_AND_ASSIGN(TypeInfo * ti,
                           tm.type_info->GetTopLevelProcTypeInfo(p));
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      BytecodeEmitter::Emit(&import_data, ti, p->config(), ParametricEnv()));
  const std::vector<Bytecode>& config_bytecodes = bf->bytecodes();
  ASSERT_EQ(config_bytecodes.size(), 7);
  const std::vector<std::string> kConfigExpected = {
      "literal (channel, channel) @ test.x:7:18-7:26",
      "expand_tuple @ test.x:7:9-7:15",
      "store 0 @ test.x:7:10-7:11",
      "store 1 @ test.x:7:13-7:14",
      "load 1 @ test.x:8:6-8:7",
      "literal u32:100 @ test.x:8:13-8:16",
      "create_tuple 2 @ test.x:8:5-8:17"};

  for (int i = 0; i < config_bytecodes.size(); i++) {
    ASSERT_EQ(config_bytecodes[i].ToString(), kConfigExpected[i]);
  }
}

TEST(BytecodeEmitterTest, SpawnedProc) {
  constexpr std::string_view kProgram = R"(
proc Child {
  c: chan<u32> in;
  x: u32;
  y: u64;

  config(c: chan<u32> in, a: u64, b: uN[128]) {
    (c, a as u32, (a + b as u64))
  }

  init {
    u64:1234
  }

  next(tok: token, a: u64) {
    let (tok, b) = recv(tok, c);
    a + x as u64 + y + b as u64
  }
}

proc Parent {
  p: chan<u32> out;
  init { () }
  config() {
    let (p, c) = chan<u32>;
    spawn Child(c, u64:100, uN[128]:200);
    (p,)
  }

  next(tok: token, state: ()) {
    ()
  }
}
)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      TypecheckedModule tm,
      ParseAndTypecheck(kProgram, "test.x", "test", &import_data));
  XLS_ASSERT_OK_AND_ASSIGN(Proc * parent,
                           tm.module->GetMemberOrError<Proc>("Parent"));
  XLS_ASSERT_OK_AND_ASSIGN(Proc * child,
                           tm.module->GetMemberOrError<Proc>("Child"));

  Block* config_body = parent->config()->body();
  EXPECT_EQ(config_body->statements().size(), 3);
  Spawn* spawn = down_cast<Spawn*>(
      std::get<Expr*>(config_body->statements().at(1)->wrapped()));
  XLS_ASSERT_OK_AND_ASSIGN(TypeInfo * parent_ti,
                           tm.type_info->GetTopLevelProcTypeInfo(parent));
  TypeInfo* child_ti =
      parent_ti->GetInvocationTypeInfo(spawn->config(), ParametricEnv())
          .value();
  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      BytecodeEmitter::Emit(&import_data, child_ti, child->config(),
                            ParametricEnv()));
  const std::vector<Bytecode>& config_bytecodes = bf->bytecodes();
  ASSERT_EQ(config_bytecodes.size(), 8);
  const std::vector<std::string> kConfigExpected = {
      "load 0 @ test.x:8:6-8:7",           //
      "load 1 @ test.x:8:9-8:10",          //
      "cast uN[32] @ test.x:8:9-8:17",     //
      "load 1 @ test.x:8:20-8:21",         //
      "load 2 @ test.x:8:24-8:25",         //
      "cast uN[64] @ test.x:8:24-8:32",    //
      "add @ test.x:8:22-8:23",            //
      "create_tuple 3 @ test.x:8:5-8:34",  //
  };
  for (int i = 0; i < config_bytecodes.size(); i++) {
    ASSERT_EQ(config_bytecodes[i].ToString(), kConfigExpected[i]);
  }

  std::vector<NameDef*> members;
  for (const Param* member : child->members()) {
    members.push_back(member->name_def());
  }
  child_ti =
      parent_ti->GetInvocationTypeInfo(spawn->next(), ParametricEnv()).value();
  XLS_ASSERT_OK_AND_ASSIGN(
      bf, BytecodeEmitter::EmitProcNext(&import_data, child_ti, child->next(),
                                        ParametricEnv(), members));
  const std::vector<Bytecode>& next_bytecodes = bf->bytecodes();
  ASSERT_EQ(next_bytecodes.size(), 17);
  const std::vector<std::string> kNextExpected = {
      "load 3 @ test.x:16:25-16:28",
      "load 0 @ test.x:16:30-16:31",
      "literal u1:1 @ test.x:16:24-16:32",
      "literal u32:0 @ test.x:16:24-16:32",
      "recv Child::c @ test.x:16:24-16:32",
      "expand_tuple @ test.x:16:9-16:17",
      "store 5 @ test.x:16:10-16:13",
      "store 6 @ test.x:16:15-16:16",
      "load 4 @ test.x:17:5-17:6",
      "load 1 @ test.x:17:9-17:10",
      "cast uN[64] @ test.x:17:9-17:17",
      "add @ test.x:17:7-17:8",
      "load 2 @ test.x:17:20-17:21",
      "add @ test.x:17:18-17:19",
      "load 6 @ test.x:17:24-17:25",
      "cast uN[64] @ test.x:17:24-17:32",
      "add @ test.x:17:22-17:23"};
  for (int i = 0; i < next_bytecodes.size(); i++) {
    ASSERT_EQ(next_bytecodes[i].ToString(), kNextExpected[i]);
  }
}

// Verifies no explosions when calling BytecodeEmitter::EmitExpression with an
// import in the NameDef environment.
TEST(BytecodeEmitterTest, EmitExpressionWithImport) {
  constexpr std::string_view kImported = R"(
pub const MY_CONST = u32:4;
)";
  constexpr std::string_view kProgram = R"(
import imported as mod

#[test]
fn main() -> u32 {
  mod::MY_CONST + u32:1
}
)";

  ImportData import_data(CreateImportDataForTest());
  XLS_ASSERT_OK_AND_ASSIGN(
      TypecheckedModule tm,
      ParseAndTypecheck(kImported, "imported.x", "imported", &import_data));

  XLS_ASSERT_OK_AND_ASSIGN(
      tm, ParseAndTypecheck(kProgram, "test.x", "test", &import_data));

  XLS_ASSERT_OK_AND_ASSIGN(TestFunction * tf, tm.module->GetTest("main"));
  Function* f = tf->fn();
  Expr* body = f->body();

  XLS_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BytecodeFunction> bf,
      BytecodeEmitter::EmitExpression(
          &import_data, tm.type_info, body, /*env=*/{},
          /*caller_bindings=*/std::nullopt));
  const std::vector<Bytecode>& bytecodes = bf->bytecodes();
  ASSERT_EQ(bytecodes.size(), 3);
  const std::vector<std::string> kNextExpected = {
      "literal u32:4 @ test.x:6:6-6:16",
      "literal u32:1 @ test.x:6:23-6:24",
      "add @ test.x:6:17-6:18"};
  for (int i = 0; i < bytecodes.size(); i++) {
    ASSERT_EQ(bytecodes[i].ToString(), kNextExpected[i]);
  }
}

}  // namespace
}  // namespace xls::dslx
