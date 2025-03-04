// Copyright 2020 The XLS Authors
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

#include "xls/dslx/frontend/parser.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "xls/common/status/matchers.h"
#include "xls/dslx/command_line_utils.h"
#include "xls/dslx/frontend/ast.h"
#include "xls/dslx/frontend/bindings.h"
#include "xls/dslx/frontend/builtins_metadata.h"
#include "xls/dslx/frontend/scanner.h"

namespace xls::dslx {

using status_testing::IsOkAndHolds;
using status_testing::StatusIs;
using testing::HasSubstr;

static const char kFilename[] = "test.x";

class ParserTest : public ::testing::Test {
 public:
  std::unique_ptr<Module> RoundTrip(
      std::string program,
      std::optional<std::string_view> target = std::nullopt) {
    scanner_.emplace(kFilename, program);
    parser_.emplace("test", &*scanner_);
    auto module_or = parser_->ParseModule();
    if (!module_or.ok()) {
      TryPrintError(module_or.status(),
                    [&](std::string_view path) -> absl::StatusOr<std::string> {
                      return program;
                    });
      XLS_EXPECT_OK(module_or) << module_or.status();
      return nullptr;
    }
    std::unique_ptr<Module> module = std::move(module_or).value();
    if (target.has_value()) {
      EXPECT_EQ(module->ToString(), *target);
    } else {
      EXPECT_EQ(module->ToString(), program);
    }

    return module;
  }

  // Note: given expression text should have no free variables other than those
  // in "predefine": those are defined a builtin name definitions (like the DSLX
  // builtins are).
  absl::StatusOr<Expr*> ParseExpr(std::string_view expr_text,
                                  absl::Span<const std::string> predefine = {},
                                  bool populate_dslx_builtins = false) {
    scanner_.emplace(kFilename, std::string{expr_text});
    parser_.emplace("test", &*scanner_);
    Bindings b;

    Module& mod = *parser_->module_;
    if (populate_dslx_builtins) {
      for (auto const& it : GetParametricBuiltins()) {
        std::string name(it.first);
        b.Add(name, mod.GetOrCreateBuiltinNameDef(name));
      }
    }

    for (const std::string& s : predefine) {
      b.Add(s, mod.GetOrCreateBuiltinNameDef(s));
    }
    auto expr_or = parser_->ParseExpression(/*bindings=*/b);
    if (!expr_or.ok()) {
      TryPrintError(expr_or.status(),
                    [&](std::string_view path) -> absl::StatusOr<std::string> {
                      return std::string{expr_text};
                    });
    }
    return expr_or;
  }

  Expr* RoundTripExpr(std::string_view expr_text,
                      absl::Span<const std::string> predefine = {},
                      bool populate_dslx_builtins = false,
                      std::optional<std::string> target = std::nullopt) {
    absl::StatusOr<Expr*> e_or =
        ParseExpr(expr_text, predefine, populate_dslx_builtins);
    if (!e_or.ok()) {
      XLS_EXPECT_OK(e_or.status());
      return nullptr;
    }
    Expr* e = e_or.value();
    if (target.has_value()) {
      EXPECT_EQ(e->ToString(), *target);
    } else {
      EXPECT_EQ(e->ToString(), expr_text);
    }

    return e;
  }

  // Allows the private ParseTypeAnnotation method to be used by subtypes (test
  // instances).
  absl::StatusOr<TypeAnnotation*> ParseTypeAnnotation(Parser& p,
                                                      Bindings& bindings) {
    return p.ParseTypeAnnotation(bindings);
  }

  std::optional<Scanner> scanner_;
  std::optional<Parser> parser_;
};

TEST(BindingsTest, BindingsStack) {
  Module module("test", /*fs_path=*/std::nullopt);
  Bindings top;
  Bindings leaf0(&top);
  Bindings leaf1(&top);

  auto* a = module.GetOrCreateBuiltinNameDef("a");
  auto* b = module.GetOrCreateBuiltinNameDef("b");
  auto* c = module.GetOrCreateBuiltinNameDef("c");

  top.Add("a", a);
  leaf0.Add("b", b);
  leaf1.Add("c", c);

  const char* kFakeFilename = "fake.x";
  Pos pos(kFakeFilename, 0, 0);
  Span span(pos, pos);

  // Everybody can resolve the binding in "top".
  EXPECT_THAT(leaf0.ResolveNodeOrError("a", span), IsOkAndHolds(a));
  EXPECT_THAT(leaf1.ResolveNodeOrError("a", span), IsOkAndHolds(a));
  EXPECT_THAT(top.ResolveNodeOrError("a", span), IsOkAndHolds(a));

  EXPECT_THAT(top.ResolveNodeOrError("b", span),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Cannot find a definition for name: \"b\"")));
  EXPECT_THAT(leaf1.ResolveNodeOrError("b", span),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Cannot find a definition for name: \"b\"")));
  EXPECT_THAT(leaf0.ResolveNodeOrError("c", span),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Cannot find a definition for name: \"c\"")));

  EXPECT_THAT(leaf0.ResolveNodeOrError("b", span), IsOkAndHolds(b));
  EXPECT_THAT(leaf1.ResolveNodeOrError("c", span), IsOkAndHolds(c));
}

TEST_F(ParserTest, TestRoundTripFailsOnSyntaxError) {
  EXPECT_NONFATAL_FAILURE(RoundTrip("invalid-program"), "ParseError:");
}

TEST_F(ParserTest, TestIdentityFunction) {
  RoundTrip(R"(fn f(x: u32) -> u32 {
    x
})");
}

TEST_F(ParserTest, TestIdentityFunctionWithLet) {
  std::unique_ptr<Module> module = RoundTrip(R"(fn f(x: u32) -> u32 {
    let y = x;
    y
})");
  std::optional<Function*> maybe_f = module->GetFunction("f");
  ASSERT_TRUE(maybe_f.has_value());
  Function* f = maybe_f.value();
  ASSERT_NE(f, nullptr);
  Block* f_body = f->body();
  ASSERT_EQ(f_body->statements().size(), 2);
}

TEST_F(ParserTest, TestBlockOfUnitNoSemi) {
  RoundTripExpr(R"({
    ()
})");
}

TEST_F(ParserTest, TestBlockOfUnitWithSemi) {
  RoundTripExpr(R"({
    ();
})");
}

TEST_F(ParserTest, TestBlockOfTwoUnits) {
  Expr* e = RoundTripExpr(R"({
    ();
    ()
})");
  ASSERT_NE(e, nullptr);
  auto* block = dynamic_cast<Block*>(e);
  ASSERT_NE(block, nullptr);
  ASSERT_EQ(block->statements().size(), 2);
  EXPECT_TRUE(
      std::holds_alternative<Expr*>(block->statements().at(0)->wrapped()));
  EXPECT_TRUE(
      std::holds_alternative<Expr*>(block->statements().at(1)->wrapped()));
}

TEST_F(ParserTest, TestTokenIdentity) {
  RoundTrip(R"(fn f(t: token) -> token {
    t
})");
}

TEST_F(ParserTest, StructDefRoundTrip) {
  RoundTrip(R"(pub struct foo<A: u32, B: bits[16]> {
    a: bits[A],
    b: bits[16][B],
})");
}

TEST_F(ParserTest, ParseErrorSpan) {
  const char* kFakeFilename = "fake.x";
  Scanner scanner(kFakeFilename, "+");
  Parser parser("test_module", &scanner);
  Bindings b;
  absl::StatusOr<Expr*> expr_or = parser.ParseExpression(b);
  ASSERT_THAT(expr_or, StatusIs(absl::StatusCode::kInvalidArgument,
                                "ParseError: fake.x:1:1-1:2 Expected start of "
                                "an expression; got: +"));
}

TEST_F(ParserTest, EmptyTupleWithComma) {
  const char* kFakeFilename = "fake.x";
  Scanner scanner(kFakeFilename, "(,)");
  Parser parser("test_module", &scanner);
  Bindings b;
  absl::StatusOr<Expr*> expr_or = parser.ParseExpression(b);
  ASSERT_THAT(
      expr_or,
      StatusIs(absl::StatusCode::kInvalidArgument,
               testing::HasSubstr(
                   "fake.x:1:2-1:3 Expected start of an expression; got: ,")));
}

TEST_F(ParserTest, ParseLet) {
  const char* text = R"({
    let x: u32 = 2;
    x
})";
  Scanner s{"test.x", std::string{text}};
  Parser p{"test", &s};
  Bindings b;
  XLS_ASSERT_OK_AND_ASSIGN(Block * block,
                           p.ParseBlockExpression(/*bindings=*/b));

  absl::Span<Statement* const> stmts = block->statements();
  ASSERT_EQ(stmts.size(), 2);

  Let* let = std::get<Let*>(stmts.at(0)->wrapped());
  NameDef* name_def = std::get<NameDef*>(let->name_def_tree()->leaf());
  EXPECT_EQ(name_def->identifier(), "x");
  EXPECT_EQ(let->type_annotation()->ToString(), "u32");
  EXPECT_EQ(let->rhs()->ToString(), "2");

  Expr* e = std::get<Expr*>(stmts.at(1)->wrapped());
  auto* name_ref = dynamic_cast<NameRef*>(e);
  EXPECT_EQ(name_ref->ToString(), "x");
}

TEST_F(ParserTest, ParseLetWildcardBinding) {
  const char* text = R"({
  let _ = 2;
})";
  Scanner s{"test.x", std::string{text}};
  Parser p{"test", &s};
  Bindings b;
  XLS_ASSERT_OK_AND_ASSIGN(Block * block,
                           p.ParseBlockExpression(/*bindings=*/b));

  ASSERT_TRUE(block->trailing_semi());
  absl::Span<Statement* const> stmts = block->statements();
  ASSERT_EQ(stmts.size(), 1);

  Let* let = std::get<Let*>(stmts.at(0)->wrapped());
  EXPECT_EQ(
      AstNodeKindToString(ToAstNode(let->name_def_tree()->leaf())->kind()),
      "wildcard pattern");
  WildcardPattern* wildcard =
      std::get<WildcardPattern*>(let->name_def_tree()->leaf());
  ASSERT_NE(wildcard, nullptr);
  ASSERT_TRUE(let->name_def_tree()->IsWildcardLeaf());
}

TEST_F(ParserTest, ParseLetExpressionWithShadowing) {
  const char* text = R"({
    let x: u32 = 2;
    let x: u32 = 4;
    x
})";
  Scanner s{"test.x", std::string{text}};
  Parser p{"test", &s};
  Bindings b;
  XLS_ASSERT_OK_AND_ASSIGN(Block * block,
                           p.ParseBlockExpression(/*bindings=*/b));

  absl::Span<Statement* const> stmts = block->statements();
  ASSERT_EQ(stmts.size(), 3);

  Let* second_let = std::get<Let*>(stmts.at(1)->wrapped());

  Expr* e = std::get<Expr*>(stmts.at(2)->wrapped());
  auto* name_ref = dynamic_cast<NameRef*>(e);
  EXPECT_EQ(name_ref->ToString(), "x");
  EXPECT_EQ(std::get<const NameDef*>(name_ref->name_def()),
            std::get<NameDef*>(second_let->name_def_tree()->leaf()));
}

TEST_F(ParserTest, ParseBlockMultiLet) {
  const char* kProgram = R"({
    let x = f();
    let y = g(x);
    x + y
})";
  Scanner s{"test.x", std::string{kProgram}};
  Parser p{"test", &s};
  Bindings bindings;
  Module& mod = p.module();
  bindings.Add("f", mod.GetOrCreateBuiltinNameDef("f"));
  bindings.Add("g", mod.GetOrCreateBuiltinNameDef("g"));
  XLS_ASSERT_OK_AND_ASSIGN(Block * block,
                           p.ParseBlockExpression(/*bindings=*/bindings));

  EXPECT_EQ(3, block->statements().size());
  Expr* add_expr = std::get<Expr*>(block->statements().back()->wrapped());
  auto* add = dynamic_cast<Binop*>(add_expr);
  NameRef* lhs = dynamic_cast<NameRef*>(add->lhs());
  const NameDef* lhs_def = std::get<const NameDef*>(lhs->name_def());
  EXPECT_NE(lhs_def->definer(), nullptr);
  EXPECT_EQ(lhs_def->definer()->ToString(), "f()");

  NameRef* rhs = dynamic_cast<NameRef*>(add->rhs());
  const NameDef* rhs_def = std::get<const NameDef*>(rhs->name_def());
  EXPECT_NE(rhs_def->definer(), nullptr);
  EXPECT_EQ(rhs_def->definer()->ToString(), "g(x)");
}

TEST_F(ParserTest, ParseIdentityFunction) {
  const char* text = "fn ident(x: bits) { x }";
  Scanner s{"test.x", std::string{text}};
  Parser p{"test", &s};
  Bindings bindings;
  XLS_ASSERT_OK_AND_ASSIGN(
      Function * f,
      p.ParseFunction(/*is_public=*/false, /*bindings=*/bindings));

  Block* block = f->body();
  ASSERT_TRUE(block != nullptr);
  absl::Span<Statement* const> stmts = block->statements();
  ASSERT_EQ(stmts.size(), 1);
  Statement* stmt = stmts.at(0);

  NameRef* body = dynamic_cast<NameRef*>(std::get<Expr*>(stmt->wrapped()));
  ASSERT_TRUE(body != nullptr);
  EXPECT_EQ(body->identifier(), "x");
}

TEST_F(ParserTest, ParseSimpleProc) {
  const char* text = R"(proc simple {
    x: u32;
    config() {
        ()
    }
    init {
        u32:0
    }
    next(tok: token, addend: u32) {
        x + addend
    }
})";

  Scanner s{"test.x", std::string{text}};
  Parser parser{"test", &s};
  Bindings bindings;
  auto proc_or =
      parser.ParseProc(/*is_public=*/false, /*outer_bindings=*/bindings);
  if (!proc_or.ok()) {
    TryPrintError(proc_or.status(),
                  [&](std::string_view path) -> absl::StatusOr<std::string> {
                    return std::string(text);
                  });
    XLS_ASSERT_OK(proc_or.status());
  }
  const Proc* p = proc_or.value();
  EXPECT_EQ(p->ToString(), text);
}

// Parses the "iota" example.
TEST_F(ParserTest, ParseProcNetwork) {
  std::string_view kModule = R"(proc producer {
    c_: chan<u32> out;
    limit_: u32;
    config(limit: u32, c: chan<u32> out) {
        (c, limit)
    }
    init {
        u32:0
    }
    next(tok: token, i: u32) {
        let tok = send(tok, c_, i);
        i + 1
    }
}
proc consumer<N: u32> {
    c_: chan<u32> in;
    config(c: chan<u32> in) {
        (c,)
    }
    init {
        u32:0
    }
    next(tok: token, i: u32) {
        let (tok1, e) = recv(tok, c_);
        i + 1
    }
}
proc main {
    config() {
        let (p, c) = chan<u32>;
        spawn producer(u32:10, p);
        spawn consumer(range(10), c);
        ()
    }
    init {
        ()
    }
    next(tok: token, state: ()) {
        ()
    }
})";

  RoundTrip(std::string(kModule));
}

// Parses the "iota" example with fifo_depth set on the internal channel.
TEST_F(ParserTest, ParseProcNetworkWithFifoDepthOnInternalChannel) {
  std::string_view kModule = R"(proc producer {
    c_: chan<u32> out;
    limit_: u32;
    config(limit: u32, c: chan<u32> out) {
        (c, limit)
    }
    init {
        u32:0
    }
    next(tok: token, i: u32) {
        let tok = send(tok, c_, i);
        i + 1
    }
}
proc consumer<N: u32> {
    c_: chan<u32> in;
    config(c: chan<u32> in) {
        (c,)
    }
    init {
        u32:0
    }
    next(tok: token, i: u32) {
        let (tok1, e) = recv(tok, c_);
        i + 1
    }
}
proc main {
    config() {
        let (p, c) = chan<u32, 2>;
        spawn producer(u32:10, p);
        spawn consumer(range(10), c);
        ()
    }
    init {
        ()
    }
    next(tok: token, state: ()) {
        ()
    }
})";

  RoundTrip(std::string(kModule));
}

TEST_F(ParserTest, ChannelsNotAsNextArgs) {
  const char* text = R"(proc producer {
    c: chan<u32> out;
    config(c: chan<u32> out) {
        (c,)
    }
    next(tok: token, i: (chan<u32> out, u32)) {
        let tok = send(tok, c, i);
        (c, i + i)
    }
})";

  Scanner s{"test.x", std::string{text}};
  Parser parser{"test", &s};
  Bindings bindings;
  auto status_or_module = parser.ParseModule();
  EXPECT_THAT(status_or_module,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Channels cannot be Proc next params.")));
}

TEST_F(ParserTest, ChannelArraysOneD) {
  constexpr std::string_view kModule = R"(proc consumer {
    c: chan<u32> out;
    config(c: chan<u32> out) {
        (c,)
    }
    init {
        u32:100
    }
    next(tok: token, i: u32) {
        recv(tok, c);
        i + i
    }
}
proc producer {
    channels: chan<u32>[32] out;
    config() {
        let (ps, cs) = chan<u32>[32];
        spawn consumer(cs[0]);
        (ps,)
    }
    init {
        ()
    }
    next(tok: token, state: ()) {
        send(tok, channels[0], u32:0);
    }
})";

  RoundTrip(std::string(kModule));
}

TEST_F(ParserTest, ChannelArraysThreeD) {
  constexpr std::string_view kModule = R"(proc consumer {
    c: chan<u32> out;
    config(c: chan<u32> out) {
        (c,)
    }
    init {
        u32:0
    }
    next(tok: token, i: u32) {
        let tok = recv(tok, c);
        i + i
    }
}
proc producer {
    channels: chan<u32>[32][64][128] out;
    config() {
        let (ps, cs) = chan<u32>[32][64][128];
        spawn consumer(cs[0]);
        (ps,)
    }
    init {
        ()
    }
    next(tok: token, state: ()) {
        send(tok, channels[0][1][2], u32:0);
    }
})";

  RoundTrip(std::string(kModule));
}

TEST_F(ParserTest, ParseSendIfAndRecvIf) {
  constexpr std::string_view kModule = R"(proc producer {
    c: chan<u32> in;
    config(c: chan<u32> in) {
        (c,)
    }
    init {
        false
    }
    next(tok: token, do_send: bool) {
        send_if(tok, c, do_send, do_send as u32);
        (!do_send,)
    }
}
proc consumer {
    c: chan<u32> in;
    config(c: chan<u32> in) {
        (c,)
    }
    init {
        false
    }
    next(tok: token, do_recv: bool) {
        let (_, foo) = recv_if(tok, c, do_recv, u32:0);
        let _ = assert_eq(foo, true);
        (!do_recv,)
    }
})";

  RoundTrip(std::string(kModule));
}

TEST_F(ParserTest, ParseSendIfAndRecvNb) {
  constexpr std::string_view kModule = R"(proc producer {
    c: chan<u32> in;
    config(c: chan<u32> in) {
        (c,)
    }
    init {
        false
    }
    next(tok: token, do_send: bool) {
        let _ = send_if(tok, c, do_send, do_send as u32);
        (!do_send,)
    }
}
proc consumer {
    c: chan<u32> in;
    config(c: chan<u32> in) {
        (c,)
    }
    init {
        ()
    }
    next(tok: token, state: ()) {
        let (_, foo, valid) = recv_non_blocking(tok, c, u32:0);
        assert_eq(!(foo ^ valid), true);
    }
})";

  RoundTrip(std::string(kModule));
}

TEST_F(ParserTest, ParseRecvIfNb) {
  constexpr std::string_view kModule = R"(proc foo {
    c: chan<u32> in;
    config(c: chan<u32> in) {
        (c,)
    }
    init {
        ()
    }
    next(tok: token, state: ()) {
        recv_if_non_blocking(tok, c, true, u32:0);
    }
})";

  RoundTrip(std::string(kModule));
}

TEST_F(ParserTest, ParseJoin) {
  constexpr std::string_view kModule = R"(proc foo {
    c0: chan<u32> out;
    c1: chan<u32> out;
    c2: chan<u32> out;
    c3: chan<u32> in;
    config(c0: chan<u32> out, c1: chan<u32> out, c2: chan<u32> out, c3: chan<u32> in) {
        (c0, c1, c2, c3)
    }
    init {
        u32:0
    }
    next(tok: token, state: u32) {
        let tok0 = send(tok, c0, state as u32);
        let tok1 = send(tok, c1, state as u32);
        let tok2 = send(tok, c2, state as u32);
        let tok3 = send(tok0, c0, state as u32);
        let tok = join(tok0, tok1, tok2, send(tok0, c0, state as u32));
        let tok = recv(tok, c3);
        state + u32:1
    }
})";

  RoundTrip(std::string(kModule));
}

TEST_F(ParserTest, ParseTestProc) {
  constexpr std::string_view kModule = R"(proc testee {
    input: chan<u32> in;
    output: chan<u32> out;
    config(input: chan<u32> in, output: chan<u32> out) {
        (input, output)
    }
    init {
        u32:0
    }
    next(tok: token, x: u32) {
        let (tok, y) = recv(tok, input);
        let tok = send(tok, output, x + y);
        (x + y,)
    }
}
#[test_proc]
proc tester {
    p: chan<u32> out;
    c: chan<u32> in;
    terminator: chan<u32> out;
    config(terminator: chan<u32> out) {
        let (input_p, input_c) = chan<u32>;
        let (output_p, output_c) = chan<u32>;
        spawn testee(input_c, output_p);
        (input_p, output_c, terminator)
    }
    init {
        u32:0
    }
    next(tok: token, iter: u32) {
        let tok = send(tok, p, u32:0);
        let tok = send(tok, p, u32:1);
        let tok = send(tok, p, u32:2);
        let tok = send(tok, p, u32:3);
        let (tok, exp) = recv(tok, c);
        assert_eq(exp, u32:0);
        let (tok, exp) = recv(tok, c);
        assert_eq(exp, u32:1);
        let (tok, exp) = recv(tok, c);
        assert_eq(exp, u32:3);
        let (tok, exp) = recv(tok, c);
        assert_eq(exp, u32:6);
        let tok = send_if(tok, terminator, iter == u32:4, true);
        (iter + u32:1,)
    }
})";

  RoundTrip(std::string(kModule));
}

TEST_F(ParserTest, ParseStructSplat) {
  const char* text = R"(struct Point {
    x: u32,
    y: u32,
}
fn f(p: Point) -> Point {
    Point { x: u32:42, ..p }
})";
  Scanner s{"test.x", std::string{text}};
  Parser parser{"test", &s};
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<Module> m, parser.ParseModule());
  XLS_ASSERT_OK_AND_ASSIGN(TypeDefinition c, m->GetTypeDefinition("Point"));
  ASSERT_TRUE(std::holds_alternative<StructDef*>(c));
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, m->GetMemberOrError<Function>("f"));

  Block* block = f->body();
  absl::Span<Statement* const> stmts = block->statements();
  ASSERT_EQ(stmts.size(), 1);
  Statement* stmt = stmts.at(0);

  SplatStructInstance* ssi =
      dynamic_cast<SplatStructInstance*>(std::get<Expr*>(stmt->wrapped()));
  ASSERT_TRUE(ssi != nullptr) << f->body()->ToString();
  NameRef* splatted = dynamic_cast<NameRef*>(ssi->splatted());
  ASSERT_TRUE(splatted != nullptr) << ssi->splatted()->ToString();
  EXPECT_EQ(splatted->identifier(), "p");
}

TEST_F(ParserTest, ConcatFunction) {
  // TODO(leary): 2021-01-24 Notably just "bits" is not a valid type here,
  // should make a test that doesn't make it through typechecking if it's not a
  // parse-time error.
  const char* text = "fn concat(x: bits, y: bits) { x ++ y }";
  Scanner s{"test.x", std::string{text}};
  Parser p{"test", &s};
  Bindings bindings;
  XLS_ASSERT_OK_AND_ASSIGN(
      Function * f,
      p.ParseFunction(/*is_public=*/false, /*bindings=*/bindings));
  EXPECT_EQ(f->params().size(), 2);

  Block* block = f->body();
  absl::Span<Statement* const> stmts = block->statements();
  ASSERT_EQ(stmts.size(), 1);
  Statement* stmt = stmts.at(0);

  Binop* body = dynamic_cast<Binop*>(std::get<Expr*>(stmt->wrapped()));
  ASSERT_TRUE(body != nullptr);
  EXPECT_EQ(body->binop_kind(), BinopKind::kConcat);
  NameRef* lhs = dynamic_cast<NameRef*>(body->lhs());
  ASSERT_TRUE(lhs != nullptr);
  EXPECT_EQ(lhs->identifier(), "x");
  NameRef* rhs = dynamic_cast<NameRef*>(body->rhs());
  EXPECT_EQ(rhs->identifier(), "y");
}

// Verifies that the parameter sequence to a function can have a trailing comma
// and it's not a parse error.
TEST_F(ParserTest, TrailingParameterComma) {
  const char* text = R"(
fn concat(
  x: bits,
  y: bits,
) {
  x ++ y
}
)";
  Scanner s{"test.x", std::string{text}};
  Parser p{"test", &s};
  Bindings bindings;
  XLS_ASSERT_OK_AND_ASSIGN(
      Function * f,
      p.ParseFunction(/*is_public=*/false, /*bindings=*/bindings));
  EXPECT_EQ(f->params().size(), 2);
}

TEST_F(ParserTest, BitSlice) {
  const char* text = R"(
fn f(x: u32) -> u8 {
  x[0:8]
}
)";
  Scanner s{"test.x", std::string{text}};
  Parser p{"test", &s};
  Bindings bindings;
  XLS_ASSERT_OK_AND_ASSIGN(
      Function * f,
      p.ParseFunction(/*is_public=*/false, /*bindings=*/bindings));

  Block* block = f->body();
  absl::Span<Statement* const> stmts = block->statements();
  ASSERT_EQ(stmts.size(), 1);
  Statement* stmt = stmts.at(0);

  auto* index = dynamic_cast<Index*>(std::get<Expr*>(stmt->wrapped()));
  ASSERT_NE(index, nullptr);
  IndexRhs rhs = index->rhs();
  ASSERT_TRUE(std::holds_alternative<Slice*>(rhs));
  auto* slice = std::get<Slice*>(rhs);
  EXPECT_EQ(slice->start()->ToString(), "0");
  EXPECT_EQ(slice->limit()->ToString(), "8");
}

TEST_F(ParserTest, LocalConstBinding) {
  const char* text = R"(fn f() -> u8 {
    const FOO = u8:42;
    FOO
})";
  RoundTrip(text);
  Scanner s{"test.x", std::string{text}};
  Parser p{"test", &s};
  Bindings bindings;
  XLS_ASSERT_OK_AND_ASSIGN(
      Function * f,
      p.ParseFunction(/*is_public=*/false, /*bindings=*/bindings));
  Block* body = f->body();
  absl::Span<Statement* const> stmts = body->statements();
  ASSERT_EQ(stmts.size(), 2);

  Let* const_let = std::get<Let*>(stmts.at(0)->wrapped());
  ASSERT_NE(const_let, nullptr);
  ASSERT_TRUE(const_let->is_const());
  EXPECT_EQ("u8:42", const_let->rhs()->ToString());

  auto* const_ref =
      dynamic_cast<ConstRef*>(std::get<Expr*>(stmts.at(1)->wrapped()));
  ASSERT_NE(const_ref, nullptr);
  const NameDef* name_def = const_ref->name_def();
  EXPECT_EQ(name_def->ToString(), "FOO");
  AstNode* definer = name_def->definer();
  EXPECT_EQ(definer, const_let);
}

TEST_F(ParserTest, ParenthesizedUnop) { RoundTripExpr("(!x)", {"x"}); }

TEST_F(ParserTest, BitSliceOfCall) { RoundTripExpr("id(x)[0:8]", {"id", "x"}); }

TEST_F(ParserTest, BitSliceOfBitSlice) { RoundTripExpr("x[0:8][4:]", {"x"}); }

TEST_F(ParserTest, BitSliceWithWidth) { RoundTripExpr("x[1+:u8]", {"x"}); }

TEST_F(ParserTest, CmpChainParensOnLhs) {
  RoundTripExpr("(x == y) == z", {"x", "y", "z"});
}

TEST_F(ParserTest, CmpChainParensOnRhs) {
  RoundTripExpr("x == (y == z)", {"x", "y", "z"});
}

TEST_F(ParserTest, CmpChainParensOnLhsAndRhs) {
  RoundTripExpr("(x == y) == (y == z)", {"x", "y", "z"});
}

TEST_F(ParserTest, ZeroMacroSimple) {
  RoundTripExpr("zero!<u32>()", {}, /*populate_dslx_builtins=*/true);
}

TEST_F(ParserTest, ZeroMacroSimpleStruct) {
  RoundTripExpr("zero!<MyType>()", {"MyType"}, /*populate_dslx_builtins=*/true);
}

TEST_F(ParserTest, ZeroMacroSimpleArray) {
  RoundTripExpr("zero!<u32[10]>()", {}, /*populate_dslx_builtins=*/true);
}

TEST_F(ParserTest, ZeroMacroSimpleBitsArray) {
  RoundTripExpr("zero!<bits[32][10]>()", {}, /*populate_dslx_builtins=*/true);
}

// TODO(google/xls#984): 2023-06-02
TEST_F(ParserTest, DISABLED_ZeroMacroSimpleStructArray) {
  const char* text = R"(zero!<MyType[10]>())";
  Scanner s{"test.x", std::string{text}};
  Parser p{"test", &s};

  Bindings b;

  Module& mod = p.module();
  for (auto const& it : GetParametricBuiltins()) {
    std::string name(it.first);
    b.Add(name, mod.GetOrCreateBuiltinNameDef(name));
  }

  NameDef* name_def = mod.Make<NameDef>(
    Span::Fake(), std::string("MyType"), nullptr);

  using StructMember = std::pair<NameDef*, TypeAnnotation*>;
  auto* struct_def = mod.Make<StructDef>(
    Span::Fake(), name_def, std::vector<ParametricBinding*>(),
    std::vector<StructMember>(), false);
  b.Add(name_def->identifier(), struct_def);

  auto expr_or = p.ParseExpression(/*bindings=*/b);
  if (!expr_or.ok()) {
    TryPrintError(expr_or.status(),
                  [&](std::string_view path) -> absl::StatusOr<std::string> {
                    return std::string{text};
                  });
  }
  ASSERT_TRUE(expr_or.ok());
}

// TODO(google/xls#984): 2023-06-02
TEST_F(ParserTest, DISABLED_ZeroMacroParametricStruct) {
  const char* text = R"(zero!<MyType<MyParm0, MyParm1>>())";
  Scanner s{"test.x", std::string{text}};
  Parser p{"test", &s};

  Bindings b;

  Module& mod = p.module();
  for (auto const& it : GetParametricBuiltins()) {
    std::string name(it.first);
    b.Add(name, mod.GetOrCreateBuiltinNameDef(name));
  }

  std::vector<ParametricBinding*> params;

  NameDef* name_def = mod.Make<NameDef>(
    Span::Fake(), std::string("MyParm0"), nullptr);
  b.Add(name_def->identifier(), name_def);
  BuiltinType builtin_type = BuiltinTypeFromString("u32").value();
  TypeAnnotation* elem_type = mod.Make<BuiltinTypeAnnotation>(
    Span::Fake(), builtin_type, mod.GetOrCreateBuiltinNameDef("u32"));
  params.push_back(mod.Make<ParametricBinding>(name_def, elem_type, nullptr));

  name_def = mod.Make<NameDef>(
    Span::Fake(), std::string("MyParm1"), nullptr);
  b.Add(name_def->identifier(), name_def);
  builtin_type = BuiltinTypeFromString("u32").value();
  elem_type = mod.Make<BuiltinTypeAnnotation>(
    Span::Fake(), builtin_type, mod.GetOrCreateBuiltinNameDef("u32"));
  params.push_back(mod.Make<ParametricBinding>(name_def, elem_type, nullptr));

  name_def = mod.Make<NameDef>(
    Span::Fake(), std::string("MyType"), nullptr);

  using StructMember = std::pair<NameDef*, TypeAnnotation*>;
  auto* struct_def = mod.Make<StructDef>(
    Span::Fake(), name_def, params,
    std::vector<StructMember>(), false);
  b.Add(name_def->identifier(), struct_def);

  auto expr_or = p.ParseExpression(/*bindings=*/b);
  if (!expr_or.ok()) {
    TryPrintError(expr_or.status(),
                  [&](std::string_view path) -> absl::StatusOr<std::string> {
                    return std::string{text};
                  });
  }
  ASSERT_TRUE(expr_or.ok());
}

// TODO(google/xls#984): 2023-06-02
TEST_F(ParserTest, DISABLED_ZeroMacroParametricStructArray) {
  const char* text = R"(zero!<MyType<MyParm0, MyParm1>[10]>())";
  Scanner s{"test.x", std::string{text}};
  Parser p{"test", &s};

  Bindings b;

  Module& mod = p.module();
  for (auto const& it : GetParametricBuiltins()) {
    std::string name(it.first);
    b.Add(name, mod.GetOrCreateBuiltinNameDef(name));
  }

  std::vector<ParametricBinding*> params;

  NameDef* name_def = mod.Make<NameDef>(
    Span::Fake(), std::string("MyParm0"), nullptr);
  b.Add(name_def->identifier(), name_def);
  BuiltinType builtin_type = BuiltinTypeFromString("u32").value();
  TypeAnnotation* elem_type = mod.Make<BuiltinTypeAnnotation>(
    Span::Fake(), builtin_type, mod.GetOrCreateBuiltinNameDef("u32"));
  params.push_back(mod.Make<ParametricBinding>(name_def, elem_type, nullptr));

  name_def = mod.Make<NameDef>(
    Span::Fake(), std::string("MyParm1"), nullptr);
  b.Add(name_def->identifier(), name_def);
  builtin_type = BuiltinTypeFromString("u32").value();
  elem_type = mod.Make<BuiltinTypeAnnotation>(
    Span::Fake(), builtin_type, mod.GetOrCreateBuiltinNameDef("u32"));
  params.push_back(mod.Make<ParametricBinding>(name_def, elem_type, nullptr));

  name_def = mod.Make<NameDef>(
    Span::Fake(), std::string("MyType"), nullptr);

  using StructMember = std::pair<NameDef*, TypeAnnotation*>;
  auto* struct_def = mod.Make<StructDef>(
    Span::Fake(), name_def, params,
    std::vector<StructMember>(), false);
  b.Add(name_def->identifier(), struct_def);

  auto expr_or = p.ParseExpression(/*bindings=*/b);
  if (!expr_or.ok()) {
    TryPrintError(expr_or.status(),
                  [&](std::string_view path) -> absl::StatusOr<std::string> {
                    return std::string{text};
                  });
  }
  ASSERT_TRUE(expr_or.ok());
}

TEST_F(ParserTest, ParseBlockWithTwoStatements) {
  RoundTripExpr(R"({
    type MyU32 = u32;
    MyU32:42
})");
}

TEST_F(ParserTest, ModuleConstWithEnumInside) {
  RoundTrip(R"(enum MyEnum : u2 {
    FOO = 0,
    BAR = 1,
}
const MY_TUPLE = (MyEnum::FOO, MyEnum::BAR) as (MyEnum, MyEnum);)");
}

TEST_F(ParserTest, Struct) {
  const char* text = R"(struct Point {
    x: u32,
    y: u32,
})";
  RoundTrip(text);
}

TEST_F(ParserTest, StructWithAccessFn) {
  const char* text = R"(struct Point {
    x: u32,
    y: u32,
}
fn f(p: Point) -> u32 {
    p.x
}
fn g(xy: u32) -> Point {
    Point { x: xy, y: xy }
})";
  RoundTrip(text);
}

TEST_F(ParserTest, ParametricWithEnumColonRefInvocation) {
  const char* text = R"(enum OneValue : u3 {
    ONE = 4,
}
fn p<X: OneValue>() -> OneValue {
    X
}
fn main() {
    p<OneValue::ONE>()
})";
  RoundTrip(text);
}

TEST_F(ParserTest, LetDestructureFlat) {
  RoundTripExpr(R"({
    let (x, y, z): (u32, u32, u32) = (1, 2, 3);
    y
})");
}

TEST_F(ParserTest, LetDestructureNested) {
  RoundTripExpr(
      R"({
    let (w, (x, (y)), z): (u32, (u32, (u32,)), u32) = (1, (2, (3,)), 4);
    y
})");
}

TEST_F(ParserTest, LetDestructureWildcard) {
  RoundTripExpr(R"({
    let (x, y, _): (u32, u32, u32) = (1, 2, 3);
    y
})");
}

TEST_F(ParserTest, For) {
  RoundTripExpr(R"({
    let accum: u32 = 0;
    let accum: u32 = for (i, accum): (u32, u32) in range(u32:0, u32:4) {
        let new_accum: u32 = accum + i;
        new_accum
    }(accum);
    accum
})",
                {"range"});
}

TEST_F(ParserTest, ForSansTypeAnnotation) {
  RoundTripExpr(
      R"({
    let init = ();
    for (i, accum) in range(u32:0, u32:4) {
        accum
    }(init)
})",
      {"range"});
}

TEST_F(ParserTest, MatchWithConstPattern) {
  RoundTrip(R"(const FOO = u32:64;
fn f(x: u32) {
    match x {
        FOO => u32:64,
        _ => u32:42,
    }
})");
}

TEST_F(ParserTest, ArrayTypeAnnotation) {
  std::string s = "u8[2]";
  scanner_.emplace(kFilename, s);
  parser_.emplace("test", &*scanner_);
  Bindings bindings;
  XLS_ASSERT_OK_AND_ASSIGN(TypeAnnotation * ta,
                           ParseTypeAnnotation(parser_.value(), bindings));

  auto* array_type = dynamic_cast<ArrayTypeAnnotation*>(ta);
  EXPECT_EQ(array_type->span(),
            Span(Pos(kFilename, 0, 0), Pos(kFilename, 0, 5)));
  EXPECT_EQ(array_type->ToString(), "u8[2]");
  EXPECT_EQ(array_type->element_type()->span(),
            Span(Pos(kFilename, 0, 0), Pos(kFilename, 0, 2)));
  EXPECT_EQ(array_type->element_type()->ToString(), "u8");
}

TEST_F(ParserTest, TupleArrayAndInt) {
  Expr* e = RoundTripExpr("(u8[4]:[1, 2, 3, 4], 7)", {}, false, std::nullopt);
  auto* tuple = dynamic_cast<XlsTuple*>(e);
  EXPECT_EQ(2, tuple->members().size());
  auto* array = tuple->members()[0];
  EXPECT_NE(dynamic_cast<ConstantArray*>(array), nullptr);
}

TEST_F(ParserTest, Cast) { RoundTripExpr("foo() as u32", {"foo"}); }

TEST_F(ParserTest, CastOfCast) { RoundTripExpr("x as s32 as u32", {"x"}); }

TEST_F(ParserTest, CheckedCast) {
  RoundTripExpr("checked_cast<u32>(foo())", {"foo"},
                /*populate_dslx_builtins=*/true, "checked_cast<u32>(foo())");
}

TEST_F(ParserTest, WideningCast) {
  RoundTripExpr("widening_cast<u32>(foo())", {"foo"},
                /*populate_dslx_builtins=*/true, "widening_cast<u32>(foo())");
}

TEST_F(ParserTest, WideningCastOfCheckedCastOfCast) {
  RoundTripExpr("widening_cast<u32>(checked_cast<u16>(x as u24))", {"x"},
                /*populate_dslx_builtins=*/true);
}

TEST_F(ParserTest, CastOfCastEnum) {
  RoundTrip(R"(enum MyEnum : u3 {
    SOME_VALUE = 0,
}
fn f(x: u8) -> MyEnum {
    x as u3 as MyEnum
})");
}

TEST_F(ParserTest, CastToTypeAlias) {
  RoundTrip(R"(type u128 = bits[128];
fn f(x: u32) -> u128 {
    x as u128
})");
}

TEST_F(ParserTest, Enum) {
  RoundTrip(R"(enum MyEnum : u2 {
    A = 0,
    B = 1,
    C = 2,
    D = 3,
})");
}

TEST_F(ParserTest, ModuleWithSemis) {
  RoundTrip(R"(fn f() -> s32 {
    let x: s32 = 42;
    x
})");
}

TEST_F(ParserTest, ModuleWithParametric) {
  RoundTrip(R"(fn parametric<X: u32, Y: u32 = {X + X}>() -> (u32, u32) {
    (X, Y)
})");
}

TEST_F(ParserTest, ParametricInvocation) { RoundTripExpr("f<u32:2>()", {"f"}); }

TEST_F(ParserTest, ParametricColonRefInvocation) {
  RoundTripExpr("f<BuiltinEnum::VALUE>()", {"f", "BuiltinEnum"});
}

TEST_F(ParserTest, ModuleWithTypeAlias) { RoundTrip("type MyType = u32;"); }

TEST_F(ParserTest, ModuleWithImport) { RoundTrip("import thing"); }

TEST_F(ParserTest, ModuleWithImportDots) { RoundTrip("import thing.subthing"); }

TEST_F(ParserTest, ModuleWithImportAs) { RoundTrip("import thing as other"); }

TEST_F(ParserTest, ConstArrayOfEnumRefs) {
  RoundTrip(R"(enum MyEnum : u3 {
    FOO = 1,
    BAR = 2,
}
const A = MyEnum[2]:[MyEnum::FOO, MyEnum::BAR];)");
}

TEST_F(ParserTest, ImplicitWidthEnum) {
  RoundTrip(R"(const A = u32:42;
const B = u32:64;
enum ImplicitWidthEnum {
    FOO = A,
    BAR = B,
})");
}

TEST_F(ParserTest, ConstWithTypeAnnotation) {
  RoundTrip(R"(const MOL: u32 = u32:42;)");
}

TEST_F(ParserTest, ConstArrayOfConstRefs) {
  RoundTrip(R"(const MOL = u32:42;
const ZERO = u32:0;
const ARR = u32[2]:[MOL, ZERO];)");
}

// As above, but uses a trailing ellipsis in the array definition.
TEST_F(ParserTest, ConstArrayOfConstRefsEllipsis) {
  RoundTrip(R"(const MOL = u32:42;
const ZERO = u32:0;
const ARR = u32[2]:[MOL, ZERO, ...];)");
}

TEST_F(ParserTest, QuickCheckDirective) {
  RoundTrip(R"(#[quickcheck]
fn foo(x: u5) -> bool {
    true
})");
}

TEST_F(ParserTest, QuickCheckDirectiveWithTestCount) {
  RoundTrip(R"(#[quickcheck(test_count=1024)]
fn foo(x: u5) -> bool {
    true
})");
}

TEST_F(ParserTest, ModuleWithTypeAliasArrayTuple) {
  RoundTrip(R"(type MyType = u32;
type MyTupleType = (MyType[2],);)");
}

TEST_F(ParserTest, ModuleWithEmptyTestFunction) {
  RoundTrip(R"(#[test]
fn example() {
    ()
})");
}

TEST_F(ParserTest, ModuleWithEmptyExternVerilogFunction) {
  RoundTrip(R"(#[extern_verilog("unit")]
fn example() {
    ()
})");
}

TEST_F(ParserTest, ModuleWithTestFunction) {
  RoundTrip(R"(fn id(x: u32) -> u32 {
    x
}
#[test]
fn id_4() {
    assert_eq(u32:4, id(u32:4))
})");
}

TEST_F(ParserTest, TypeAliasForTupleWithConstSizedArray) {
  RoundTrip(R"(const HOW_MANY_THINGS = u32:42;
type MyTupleType = (u32[HOW_MANY_THINGS],);
fn get_things(x: MyTupleType) -> u32[HOW_MANY_THINGS] {
    x[0]
})");
}

TEST_F(ParserTest, ArrayOfNameRefs) {
  RoundTripExpr("[a, b, c, d]", {"a", "b", "c", "d"});
}

TEST_F(ParserTest, EmptyTuple) {
  Expr* e = RoundTripExpr("()", {}, false, std::nullopt);
  auto* tuple = dynamic_cast<XlsTuple*>(e);
  ASSERT_NE(tuple, nullptr);
  EXPECT_TRUE(tuple->empty());
}

TEST_F(ParserTest, Match) {
  RoundTripExpr(R"(match x {
    u32:42 => u32:64,
    _ => u32:42,
})",
                /*predefine=*/{"x"});
}

TEST_F(ParserTest, MatchFreevars) {
  Expr* e = RoundTripExpr(R"(match x {
    y => z,
})",
                          {"x", "y", "z"});
  FreeVariables fv = GetFreeVariables(e, &e->span().start());
  EXPECT_THAT(fv.Keys(), testing::ContainerEq(
                             absl::flat_hash_set<std::string>{"x", "y", "z"}));
}

TEST_F(ParserTest, ForFreevars) {
  Expr* e = RoundTripExpr(R"(for (i, accum): (u32, u32) in range(u32:4) {
    let new_accum: u32 = accum + i + j;
    new_accum
}(u32:0))",
                          {"range", "j"});
  FreeVariables fv = GetFreeVariables(e, &e->span().start());
  EXPECT_THAT(fv.Keys(), testing::ContainerEq(
                             absl::flat_hash_set<std::string>{"j", "range"}));
}

TEST_F(ParserTest, EmptyTernary) { RoundTripExpr("if true {} else {}"); }

TEST_F(ParserTest, TernaryConditional) {
  RoundTripExpr("if true { u32:42 } else { u32:24 }", {});

  RoundTripExpr(R"(if really_long_identifier_so_that_this_is_too_many_chars {
    u32:42
} else {
    u32:24
})",
                {"really_long_identifier_so_that_this_is_too_many_chars"});
}

TEST_F(ParserTest, LadderedConditional) {
  RoundTripExpr("if true { u32:42 } else if false { u32:33 } else { u32:24 }");

  RoundTripExpr(
      R"(if really_long_identifier_so_that_this_is_too_many_chars {
    u32:42
} else if another_really_long_identifier_so_that_this_is_too_many_chars {
    u32:22
} else {
    u32:24
})",
      {"really_long_identifier_so_that_this_is_too_many_chars",
       "another_really_long_identifier_so_that_this_is_too_many_chars"});
}

TEST_F(ParserTest, TernaryWithComparisonTest) {
  RoundTripExpr("if a <= b { u32:42 } else { u32:24 }", {"a", "b"});
}

TEST_F(ParserTest, TernaryWithComparisonToColonRefTest) {
  RoundTripExpr("if a <= m::b { u32:42 } else { u32:24 }", {"a", "m"});
}

TEST_F(ParserTest, ForInWithColonRefAsRangeLimit) {
  RoundTripExpr(R"(for (x, s) in u32:0..m::SOME_CONST {
    x
}(i))",
                {"m", "i"});
}

TEST_F(ParserTest, TernaryWithOrExpressionTest) {
  RoundTripExpr("if a || b { u32:42 } else { u32:24 }", {"a", "b"});
}

TEST_F(ParserTest, TernaryWithComparisonStructInstanceTest) {
  RoundTrip(R"(struct MyStruct {
    x: u32
}
fn f(a: MyStruct) -> u32 {
    if a.x <= MyStruct { x: u32:42 }.x { u32:42 } else { u32:24 }
})",
            /*target=*/R"(struct MyStruct {
    x: u32,
}
fn f(a: MyStruct) -> u32 {
    if a.x <= MyStruct { x: u32:42 }.x { u32:42 } else { u32:24 }
})");
}

TEST_F(ParserTest, ConstantArray) {
  Expr* e = RoundTripExpr("u32[2]:[0, 1]", {}, false, std::nullopt);
  ASSERT_TRUE(dynamic_cast<ConstantArray*>(e) != nullptr);
}

TEST_F(ParserTest, DoubleNegation) { RoundTripExpr("!!x", {"x"}, false); }

TEST_F(ParserTest, LogicalOperatorPrecedence) {
  Expr* e = RoundTripExpr("!a || !b && c", {"a", "b", "c"});
  auto* binop = dynamic_cast<Binop*>(e);
  EXPECT_EQ(binop->binop_kind(), BinopKind::kLogicalOr);
  auto* binop_rhs = dynamic_cast<Binop*>(binop->rhs());
  EXPECT_EQ(binop_rhs->binop_kind(), BinopKind::kLogicalAnd);
  auto* unop = dynamic_cast<Unop*>(binop_rhs->lhs());
  EXPECT_EQ(unop->unop_kind(), UnopKind::kInvert);
}

TEST_F(ParserTest, LogicalEqualityPrecedence) {
  Expr* e = RoundTripExpr("a ^ !b == f()", {"a", "b", "f"});
  auto* binop = dynamic_cast<Binop*>(e);
  EXPECT_EQ(binop->binop_kind(), BinopKind::kEq);
  auto* binop_lhs = dynamic_cast<Binop*>(binop->lhs());
  EXPECT_EQ(binop_lhs->binop_kind(), BinopKind::kXor);
  auto* unop = dynamic_cast<Unop*>(binop_lhs->rhs());
  EXPECT_EQ(unop->unop_kind(), UnopKind::kInvert);
}

TEST_F(ParserTest, CastVsComparatorPrecedence) {
  Expr* e = RoundTripExpr("x >= y as u32", /*predefine=*/{"x", "y"});
  auto* binop = dynamic_cast<Binop*>(e);
  EXPECT_EQ(binop->binop_kind(), BinopKind::kGe);
  auto* cast = dynamic_cast<Cast*>(binop->rhs());
  ASSERT_NE(cast, nullptr);
  auto* casted_name_ref = dynamic_cast<NameRef*>(cast->expr());
  ASSERT_NE(casted_name_ref, nullptr);
  EXPECT_EQ(casted_name_ref->identifier(), "y");
}

TEST_F(ParserTest, CastVsUnaryPrecedence) {
  Expr* e = RoundTripExpr("-x as s32", /*predefine=*/{"x", "y"});
  auto* cast = dynamic_cast<Cast*>(e);
  ASSERT_NE(cast, nullptr);
  EXPECT_EQ(cast->type_annotation()->ToString(), "s32");
}

TEST_F(ParserTest, NameDefTree) {
  RoundTripExpr(R"({
    let (a, (b, (c, d), e), f) = x;
    a
})",
                {"x"});
}

TEST_F(ParserTest, Strings) {
  RoundTripExpr(R"({
    let x = "dummy --> \" <-- string";
    x
})");
  RoundTripExpr(R"({
    let x = "dummy --> \"";
    x
})");
}

TEST_F(ParserTest, TupleIndex) {
  const char* text = R"(
fn f(x: u32) -> u8 {
    (u32:6, u32:7).1
}
)";
  Scanner s{"test.x", std::string{text}};
  Parser p{"test", &s};
  Bindings bindings;
  XLS_ASSERT_OK_AND_ASSIGN(
      Function * f,
      p.ParseFunction(/*is_public=*/false, /*bindings=*/bindings));

  Block* body = f->body();
  absl::Span<Statement* const> stmts = body->statements();
  ASSERT_EQ(stmts.size(), 1);

  auto* tuple_index =
      dynamic_cast<TupleIndex*>(std::get<Expr*>(stmts.at(0)->wrapped()));
  ASSERT_NE(tuple_index, nullptr);

  Expr* lhs = tuple_index->lhs();
  EXPECT_EQ(lhs->ToString(), "(u32:6, u32:7)");
  Number* index = tuple_index->index();
  EXPECT_EQ(index->ToString(), "1");

  RoundTripExpr(R"({
    let foo = tuple.0;
    foo
})",
                {"tuple"});
  RoundTripExpr(R"({
    let foo = (u32:6, u32:7).1;
    foo
})",
                {"tuple"});
}

TEST_F(ParserTest, BlockWithinBlock) {
  const char* kInput = R"({
    let a = u32:0;
    let b = {
        let c = u32:1;
        c
    };
    let d = u32:2;
})";
  RoundTripExpr(kInput);
}

TEST_F(ParserTest, UnrollFor) {
  RoundTripExpr(
      R"({
    let bar = u32:0;
    let res = unroll_for! (i, acc) in range(u32:0, u32:4) {
        let foo = i + 1;
    }(u32:0);
    let baz = u32:0;
    res
})",
      /*predefine=*/{"range"});
}

TEST_F(ParserTest, Range) {
  RoundTripExpr(R"({
    let foo = u32:8..u32:16;
    foo
})");
  RoundTripExpr(R"({
    let foo = a..b;
    foo
})",
                {"a", "b"});
}

TEST_F(ParserTest, BuiltinFailWithLabels) {
  constexpr std::string_view kProgram = R"(fn main(x: u32) -> u32 {
    let _ = if x == u32:7 { fail!("x_is_7", u32:0) } else { u32:0 };
    let _ = {
        if x == u32:8 { fail!("x_is_8", u32:0) } else { u32:0 }
    };
    x
})";
  RoundTrip(std::string(kProgram));
}

TEST_F(ParserTest, ProcWithInit) {
  constexpr std::string_view kProgram = R"(proc foo {
    member: u32;
    config() {
        (u32:1,)
    }
    init {
        u32:0
    }
    next(tok: token, state: u32) {
        state
    }
})";

  RoundTrip(std::string(kProgram));
}

// -- Parse-time errors

TEST_F(ParserTest, BadEnumRef) {
  const char* text = R"(
enum MyEnum : u1 {
    FOO = 0
}

fn my_fun() -> MyEnum {
    FOO  // Should be qualified as MyEnum::FOO!
}
)";
  Scanner s{"test.x", std::string{text}};
  Parser parser{"test", &s};
  auto module_status = parser.ParseModule();
  ASSERT_THAT(module_status, StatusIs(absl::StatusCode::kInvalidArgument,
                                      "ParseError: test.x:7:5-7:8 Cannot find "
                                      "a definition for name: \"FOO\""));
}

TEST_F(ParserTest, ProcConfigCantSeeMembers) {
  constexpr std::string_view kProgram = R"(
proc main {
    x12: chan<u8> in;
    config(x27: chan<u8> in) {
        (x12,)
    }
    next(x0: token) {
        ()
    }
})";
  Scanner s{"test.x", std::string{kProgram}};
  Parser parser{"test", &s};
  auto module_status = parser.ParseModule();
  ASSERT_THAT(
      module_status,
      StatusIs(absl::StatusCode::kInvalidArgument,
               "ParseError: test.x:5:10-5:13 "
               "Cannot find a definition for name: \"x12\"; "
               "\"x12\" is a proc member, but those cannot be referenced "
               "from within a proc config function."));
}

TEST_F(ParserTest, NumberSpan) {
  XLS_ASSERT_OK_AND_ASSIGN(Expr * e, ParseExpr("u32:42"));
  auto* number = dynamic_cast<Number*>(e);
  ASSERT_NE(number, nullptr);
  // TODO(https://github.com/google/xls/issues/438): 2021-05-24 Fix the
  // parsing/reporting of number spans so that the span starts at 0,0.
  EXPECT_EQ(number->span(), Span(Pos(kFilename, 0, 4), Pos(kFilename, 0, 6)));
}

TEST_F(ParserTest, DetectsDuplicateFailLabels) {
  constexpr std::string_view kProgram = R"(
fn main(x: u32) -> u32 {
    let _ = if x == u32:7 { fail!("x_is_7", u32:0) } else { u32:0 };
    let _ = if x == u32:7 { fail!("x_is_7", u32:0) } else { u32:0 };
    x
}
)";

  Scanner s{"test.x", std::string(kProgram)};
  Parser parser{"test", &s};
  absl::StatusOr<std::unique_ptr<Module>> module_or = parser.ParseModule();
  ASSERT_FALSE(module_or.ok()) << module_or.status();
  XLS_LOG(INFO) << "status: " << module_or.status();
  EXPECT_THAT(module_or.status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("A fail label must be unique")));
}

// Verifies that we can walk backwards through a tree. In this case, from the
// terminal node to the defining expr.
TEST(ParserBackrefTest, CanFindDefiner) {
  constexpr std::string_view kProgram = R"(
fn main() -> u32 {
    let foo = u32:0 + u32:1;
    let bar = u32:3 + foo;
    let baz = bar + foo;
    foo
}
)";

  Scanner s{"test.x", std::string(kProgram)};
  Parser parser{"test", &s};
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<Module> module,
                           parser.ParseModule());
  XLS_ASSERT_OK_AND_ASSIGN(Function * f,
                           module->GetMemberOrError<Function>("main"));

  Block* body = f->body();
  absl::Span<Statement* const> stmts = body->statements();
  ASSERT_EQ(stmts.size(), 4);

  // Get the terminal expr.
  Expr* current_expr = std::get<Expr*>(stmts.back()->wrapped());
  NameRef* nameref = dynamic_cast<NameRef*>(current_expr);
  ASSERT_NE(nameref, nullptr);

  Let* foo_parent = std::get<Let*>(stmts.at(0)->wrapped());
  ASSERT_NE(foo_parent, nullptr);
  // The easiest way to verify what we've got the right node is just to do a
  // string comparison, even if it's not pretty.
  EXPECT_EQ(foo_parent->rhs()->ToString(), "u32:0 + u32:1");
}

TEST_F(ParserTest, ChainedEqualsComparisonError) {
  constexpr std::string_view kProgram = R"(
fn main(x: u32, y: u32, z: bool) -> bool {
    x == y == z
}
)";

  Scanner s{"test.x", std::string(kProgram)};
  Parser parser{"test", &s};
  EXPECT_THAT(parser.ParseModule(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("ParseError: test.x:3:12-3:14 comparison "
                                 "operators cannot be chained")));
}

TEST_F(ParserTest, ChainedLtComparisonError) {
  constexpr std::string_view kProgram = R"(
fn main(x: u32, y: u32, z: bool) -> bool {
    x < y < z
}
)";

  Scanner s{"test.x", std::string(kProgram)};
  Parser parser{"test", &s};
  EXPECT_THAT(parser.ParseModule(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("ParseError: test.x:3:11-3:12 comparison "
                                 "operators cannot be chained")));
}

TEST_F(ParserTest, ChainedLtGtComparisonError) {
  constexpr std::string_view kProgram = R"(
fn main(x: u32, y: u32, z: bool) -> bool {
    x < y > z
}
)";

  Scanner s{"test.x", std::string(kProgram)};
  Parser parser{"test", &s};
  EXPECT_THAT(
      parser.ParseModule(),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Expected a '(' after parametrics for function invocation.")));
}

TEST_F(ParserTest, ChannelDeclWithFifoDepthExpression) {
  constexpr std::string_view kProgram = R"(proc foo<N: u32, M: u32> {
    c_in: chan<u32> in;
    c_out: chan<u32> out;
    config () {
        let (c_p, c_c) = chan<u32, {N + M}>;
        (c_p, c_c)
    }
    init {}
    next(tok: token, state: ()) {
      ()
    }
}
)";
  Scanner s{"test.x", std::string(kProgram)};
  Parser parser{"test", &s};
  XLS_EXPECT_OK(parser.ParseModule());
}

}  // namespace xls::dslx
