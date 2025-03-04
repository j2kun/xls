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

#include "xls/ir/value_helpers.h"

#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "xls/common/logging/logging.h"
#include "xls/common/status/ret_check.h"
#include "xls/common/status/status_macros.h"
#include "xls/ir/type.h"

namespace xls {
namespace {

Value ValueOfType(Type* type,
                  const std::function<Bits(int64_t bit_count)>& fbits) {
  switch (type->kind()) {
    case TypeKind::kBits:
      return Value(fbits(type->AsBitsOrDie()->bit_count()));
    case TypeKind::kTuple: {
      std::vector<Value> elements;
      for (Type* element_type : type->AsTupleOrDie()->element_types()) {
        elements.push_back(ValueOfType(element_type, fbits));
      }
      return Value::Tuple(elements);
    }
    case TypeKind::kArray: {
      std::vector<Value> elements;
      for (int64_t i = 0; i < type->AsArrayOrDie()->size(); ++i) {
        elements.push_back(
            ValueOfType(type->AsArrayOrDie()->element_type(), fbits));
      }
      return Value::Array(elements).value();
    }
    case TypeKind::kToken:
      return Value::Token();
  }
  XLS_LOG(FATAL) << "Invalid kind: " << type->kind();
}

void ValueLeafNodes(const Value& value, std::vector<Value>& leaves) {
  if (value.IsBits() || value.IsToken()) {
    leaves.push_back(value);
    return;
  }
  for (int64_t i = 0; i < value.size(); ++i) {
    ValueLeafNodes(value.element(i), leaves);
  }
}

}  // namespace

Value ZeroOfType(Type* type) {
  return ValueOfType(type, [](int64_t bit_count) {
    return UBits(0, /*bit_count=*/bit_count);
  });
}

Value AllOnesOfType(Type* type) {
  return ValueOfType(type, [](int64_t bit_count) {
    return bit_count == 0 ? Bits(0) : SBits(-1, /*bit_count=*/bit_count);
  });
}

Value F32ToTuple(float value) {
  uint32_t x = absl::bit_cast<uint32_t>(value);
  bool sign = (x >> 31) != 0u;
  uint8_t exp = x >> 23;
  uint32_t fraction = x & Mask(23);
  return Value::Tuple({
      Value(Bits::FromBytes({static_cast<const unsigned char>(sign)},
                            /*bit_count=*/1)),
      Value(Bits::FromBytes({exp}, /*bit_count=*/8)),
      Value(UBits(fraction, /*bit_count=*/23)),
  });
}

absl::StatusOr<float> TupleToF32(const Value& v) {
  XLS_RET_CHECK(v.IsTuple()) << v.ToString();
  XLS_RET_CHECK_EQ(v.elements().size(), 3) << v.ToString();
  XLS_RET_CHECK_EQ(v.element(0).bits().bit_count(), 1);
  XLS_ASSIGN_OR_RETURN(uint32_t sign, v.element(0).bits().ToUint64());
  XLS_RET_CHECK_EQ(v.element(1).bits().bit_count(), 8);
  XLS_ASSIGN_OR_RETURN(uint32_t exp, v.element(1).bits().ToUint64());
  XLS_RET_CHECK_EQ(v.element(2).bits().bit_count(), 23);
  XLS_ASSIGN_OR_RETURN(uint32_t fraction, v.element(2).bits().ToUint64());
  // Validate the values were all appropriate.
  XLS_DCHECK_EQ(sign, sign & Mask(1));
  XLS_DCHECK_EQ(exp, exp & Mask(8));
  XLS_DCHECK_EQ(fraction, fraction & Mask(23));
  // Reconstruct the float.
  uint32_t x = (sign << 31) | (exp << 23) | fraction;
  return absl::bit_cast<float>(x);
}

absl::StatusOr<Value> LeafTypeTreeToValue(const LeafTypeTree<Value>& tree) {
  Type* type = tree.type();
  if (type->IsTuple()) {
    std::vector<Value> values;
    for (int64_t i = 0; i < type->AsTupleOrDie()->size(); ++i) {
      XLS_ASSIGN_OR_RETURN(Value value,
                           LeafTypeTreeToValue(tree.CopySubtree({i})));
      values.push_back(value);
    }
    return Value::TupleOwned(std::move(values));
  }
  if (type->IsArray()) {
    std::vector<Value> values;
    for (int64_t i = 0; i < type->AsArrayOrDie()->size(); ++i) {
      XLS_ASSIGN_OR_RETURN(Value value,
                           LeafTypeTreeToValue(tree.CopySubtree({i})));
      values.push_back(value);
    }
    return Value::ArrayOrDie(values);
  }
  return tree.Get({});
}

absl::StatusOr<LeafTypeTree<Value>> ValueToLeafTypeTree(const Value& value,
                                                        Type* type) {
  XLS_RET_CHECK(ValueConformsToType(value, type));
  std::vector<Value> leaf_nodes;
  ValueLeafNodes(value, leaf_nodes);
  return LeafTypeTree<Value>(type, leaf_nodes);
}

}  // namespace xls
