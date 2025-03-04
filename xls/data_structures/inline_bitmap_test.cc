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

#include "xls/data_structures/inline_bitmap.h"

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xls {
namespace {

using testing::ElementsAre;

TEST(InlineBitmapTest, FromWord) {
  {
    auto b =
        InlineBitmap::FromWord(/*word=*/0, /*bit_count=*/0, /*fill=*/false);
    EXPECT_TRUE(b.IsAllZeroes());
  }

  {
    auto b = InlineBitmap::FromWord(/*word=*/0xFFFFFFFFFFFFFFFFULL,
                                    /*bit_count=*/0, /*fill=*/false);
    EXPECT_TRUE(b.IsAllZeroes());
  }

  {
    auto b = InlineBitmap::FromWord(/*word=*/0xFFFFFFFFFFFFFFFFULL,
                                    /*bit_count=*/1, /*fill=*/false);
    EXPECT_EQ(b.Get(0), 1);
    EXPECT_TRUE(b.IsAllOnes());
  }

  {
    auto b = InlineBitmap::FromWord(/*word=*/0x123456789abcdef0,
                                    /*bit_count=*/64, /*fill=*/false);
    EXPECT_EQ(b.GetWord(0), 0x123456789abcdef0);
  }

  {
    auto b = InlineBitmap::FromWord(/*word=*/0x123456789abcdef0,
                                    /*bit_count=*/128, /*fill=*/false);
    EXPECT_EQ(b.GetWord(0), 0x123456789abcdef0);
    EXPECT_EQ(b.GetWord(1), 0);
  }

  {
    auto b = InlineBitmap::FromWord(/*word=*/0x123456789abcdef0,
                                    /*bit_count=*/128, /*fill=*/true);
    EXPECT_EQ(b.GetWord(0), 0x123456789abcdef0);
    EXPECT_EQ(b.GetWord(1), 0xffffffffffffffff);
  }

  {
    auto b = InlineBitmap::FromWord(/*word=*/0x123456789abcdef0,
                                    /*bit_count=*/101, /*fill=*/true);
    EXPECT_EQ(b.GetWord(0), 0x123456789abcdef0);
    EXPECT_EQ(b.GetWord(1), 0x0000001fffffffff);
  }

  {
    auto b = InlineBitmap::FromWord(/*word=*/0x123456789abcdef0,
                                    /*bit_count=*/101, /*fill=*/false);
    EXPECT_EQ(b.GetWord(0), 0x123456789abcdef0);
    EXPECT_EQ(b.GetWord(1), 0);
  }
}

TEST(InlineBitmapTest, SetRange) {
  InlineBitmap b(/*bit_count=*/3);
  b.SetRange(0, 0, true);
  EXPECT_TRUE(b.IsAllZeroes());
  b.SetRange(1, 2);
  EXPECT_FALSE(b.Get(0));
  EXPECT_TRUE(b.Get(1));
  EXPECT_FALSE(b.Get(2));
  b.SetRange(0, 3);
  EXPECT_TRUE(b.IsAllOnes());
  b.SetRange(0, 3, false);
  EXPECT_TRUE(b.IsAllZeroes());
}

TEST(InlineBitmapTest, SetAllBitsToFalse) {
  InlineBitmap b(/*bit_count=*/3);
  EXPECT_TRUE(b.IsAllZeroes());
  b.Set(1);
  EXPECT_FALSE(b.IsAllZeroes());
  b.SetAllBitsToFalse();
  EXPECT_TRUE(b.IsAllZeroes());
}

TEST(InlineBitmapTest, OneBitBitmap) {
  InlineBitmap b(/*bit_count=*/1);

  // Initialized with zeros.
  EXPECT_EQ(b.Get(0), 0);
  EXPECT_TRUE(b.IsAllZeroes());
  EXPECT_FALSE(b.IsAllOnes());

  b.Set(0, false);
  EXPECT_TRUE(b.IsAllZeroes());
  EXPECT_FALSE(b.IsAllOnes());
  EXPECT_EQ(b.Get(0), 0);

  b.Set(0, true);
  EXPECT_EQ(b.Get(0), 1);
  EXPECT_TRUE(b.IsAllOnes());
  EXPECT_FALSE(b.IsAllZeroes());

  b.Set(0, false);
  EXPECT_EQ(b.Get(0), 0);
  EXPECT_TRUE(b.IsAllZeroes());
  EXPECT_FALSE(b.IsAllOnes());

  InlineBitmap b1(/*bit_count=*/1);
  EXPECT_EQ(b, b1);
  EXPECT_EQ(b1, b);
  b1.Set(0, true);
  EXPECT_NE(b1, b);
  b1.Set(0, false);
  EXPECT_EQ(b1, b);

  InlineBitmap b2(/*bit_count=*/2);
  EXPECT_NE(b2, b);
  EXPECT_NE(b, b2);
}

TEST(InlineBitmapTest, TwoBitBitmap) {
  InlineBitmap b(/*bit_count=*/2);
  EXPECT_TRUE(b.IsAllZeroes());
  EXPECT_FALSE(b.IsAllOnes());
  EXPECT_EQ(2, b.bit_count());

  b.Set(0);
  EXPECT_FALSE(b.IsAllZeroes());
  EXPECT_FALSE(b.IsAllOnes());
  EXPECT_EQ(b.Get(0), true);
  EXPECT_EQ(b.Get(1), false);

  b.Set(1);
  EXPECT_FALSE(b.IsAllZeroes());
  EXPECT_TRUE(b.IsAllOnes());
  EXPECT_EQ(b.Get(0), true);
  EXPECT_EQ(b.Get(1), true);

  EXPECT_EQ(b, b);
}

TEST(InlineBitmapTest, SixtyFiveBitBitmap) {
  InlineBitmap b(/*bit_count=*/65);
  EXPECT_TRUE(b.IsAllZeroes());
  EXPECT_FALSE(b.IsAllOnes());
  EXPECT_EQ(65, b.bit_count());

  b.Set(0, true);
  EXPECT_FALSE(b.IsAllZeroes());
  EXPECT_FALSE(b.IsAllOnes());
  EXPECT_EQ(b.Get(0), true);
  EXPECT_EQ(b.Get(1), false);
  EXPECT_EQ(b.Get(64), false);
  EXPECT_EQ(b, b);

  b.Set(0, false);
  b.Set(64, true);
  EXPECT_FALSE(b.IsAllZeroes());
  EXPECT_FALSE(b.IsAllOnes());
  EXPECT_EQ(b.Get(0), false);
  EXPECT_EQ(b.Get(1), false);
  EXPECT_EQ(b.Get(64), true);
  EXPECT_EQ(b, b);

  InlineBitmap empty(/*bit_count=*/65);
  EXPECT_NE(b, empty);
}

TEST(InlineBitmapTest, BytesAndBits) {
  InlineBitmap b(/*bit_count=*/16);
  b.SetByte(0, 0x80);  // Bit 7
  EXPECT_TRUE(b.Get(7));
  EXPECT_FALSE(b.Get(0));
  EXPECT_FALSE(b.Get(8));
  b.SetByte(1, 0x01);  // Bit 8
  EXPECT_TRUE(b.Get(8));
  EXPECT_FALSE(b.Get(15));
}

TEST(InlineBitmapTest, GetSetBytesAndWords) {
  {
    InlineBitmap b16(/*bit_count=*/16);
    b16.SetByte(0, 0xaa);
    b16.SetByte(1, 0xbb);
    EXPECT_EQ(b16.GetWord(0), 0xbbaa) << std::hex << b16.GetWord(0);
  }

  {
    InlineBitmap b9(/*bit_count=*/9);
    b9.SetByte(0, 0xaa);
    b9.SetByte(1, 0xbb);
    EXPECT_EQ(b9.GetWord(0), 0x1aa) << std::hex << b9.GetWord(0);
  }

  {
    InlineBitmap b(/*bit_count=*/64);
    b.SetByte(0, 0xf0);
    b.SetByte(1, 0xde);
    b.SetByte(2, 0xbc);
    b.SetByte(3, 0x9a);
    b.SetByte(4, 0x78);
    b.SetByte(5, 0x56);
    b.SetByte(6, 0x34);
    b.SetByte(7, 0x12);
    EXPECT_EQ(b.GetWord(0), 0x123456789abcdef0) << std::hex << b.GetWord(0);
  }

  {
    InlineBitmap b(/*bit_count=*/16);
    b.SetByte(0, 0xf0);
    b.SetByte(1, 0xde);
    EXPECT_EQ(b.GetWord(0), 0xdef0) << std::hex << b.GetWord(0);
  }

  {
    InlineBitmap b(/*bit_count=*/65);
    b.SetByte(7, 0xff);
    b.SetByte(8, 0x1);
    EXPECT_EQ(b.GetWord(0), 0xff00000000000000) << std::hex << b.GetWord(0);
    EXPECT_EQ(b.GetWord(1), 0x1) << std::hex << b.GetWord(1);
  }

  {
    InlineBitmap b(/*bit_count=*/65);
    b.SetByte(7, 0xff);
    // Only bits 0 of this byte is in range, so the higher bits should be masked
    // off.
    b.SetByte(8, 0xff);
    EXPECT_EQ(b.GetWord(0), 0xff00000000000000) << std::hex << b.GetWord(0);
    EXPECT_EQ(b.GetWord(1), 0x1) << std::hex << b.GetWord(1);
  }
}

TEST(InlineBitmapTest, FromToBytes) {
  {
    InlineBitmap b = InlineBitmap::FromBytes(0, absl::Span<const uint8_t>());
    EXPECT_EQ(b.bit_count(), 0);
    b.WriteBytesToBuffer(absl::Span<uint8_t>());
  }
  {
    InlineBitmap b = InlineBitmap::FromBytes(1, {0x1});
    EXPECT_EQ(b.bit_count(), 1);
    std::vector<uint8_t> bytes(1);
    b.WriteBytesToBuffer(absl::MakeSpan(bytes));
    EXPECT_THAT(bytes, ElementsAre(0x01));
  }
  {
    // Extra bits should be masked off.
    InlineBitmap b = InlineBitmap::FromBytes(1, {0xff});
    EXPECT_EQ(b.bit_count(), 1);
    std::vector<uint8_t> bytes(1);
    b.WriteBytesToBuffer(absl::MakeSpan(bytes));
    EXPECT_THAT(bytes, ElementsAre(0x01));
  }
  {
    InlineBitmap b = InlineBitmap::FromBytes(32, {0x01, 0x02, 0x03, 0x04});
    // Verify the endianness is as expected (little-endian).
    EXPECT_TRUE(b.Get(0));
    EXPECT_FALSE(b.Get(1));
    EXPECT_FALSE(b.Get(8));
    EXPECT_TRUE(b.Get(9));
    EXPECT_EQ(b.bit_count(), 32);
    std::vector<uint8_t> bytes(4);
    b.WriteBytesToBuffer(absl::MakeSpan(bytes));
    EXPECT_THAT(bytes, ElementsAre(0x01, 0x02, 0x03, 0x04));
  }
  {
    InlineBitmap b = InlineBitmap::FromBytes(
        128, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
              0x0b, 0x0c, 0x0d, 0x0e, 0x0f});
    // Verify the endianness is as expected (little-endian).
    EXPECT_FALSE(b.Get(0));
    EXPECT_FALSE(b.Get(1));
    EXPECT_FALSE(b.Get(2));
    EXPECT_FALSE(b.Get(3));

    EXPECT_TRUE(b.Get(8));
    EXPECT_FALSE(b.Get(9));
    EXPECT_FALSE(b.Get(10));
    EXPECT_FALSE(b.Get(11));

    EXPECT_EQ(b.bit_count(), 128);
    std::vector<uint8_t> bytes(16);
    b.WriteBytesToBuffer(absl::MakeSpan(bytes));
    EXPECT_THAT(
        bytes, ElementsAre(0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                           0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f));
  }
}

TEST(InlineBitmapTest, UnsignedComparisons) {
  {
    InlineBitmap a(/*bit_count=*/0);
    InlineBitmap b(/*bit_count=*/65);
    // a == b
    EXPECT_EQ(a.UCmp(a), 0);
    EXPECT_EQ(a.UCmp(b), 0);
    EXPECT_EQ(b.UCmp(a), 0);
    EXPECT_EQ(b.UCmp(b), 0);
  }

  {
    auto a = InlineBitmap::FromWord(/*word=*/0,
                                    /*bit_count=*/0, /*fill=*/false);
    auto b = InlineBitmap::FromWord(/*word=*/0x123456789abcdef0,
                                    /*bit_count=*/64, /*fill=*/false);
    // a < b
    EXPECT_EQ(a.UCmp(b), -1);
    EXPECT_EQ(b.UCmp(a), 1);
  }

  {
    auto a = InlineBitmap::FromWord(/*word=*/0x123456789abcdef0,
                                    /*bit_count=*/64, /*fill=*/false);
    auto b = InlineBitmap::FromWord(/*word=*/0x123456789abcdef1,
                                    /*bit_count=*/64, /*fill=*/false);
    // a < b
    EXPECT_EQ(a.UCmp(b), -1);
    EXPECT_EQ(b.UCmp(a), 1);
  }

  {
    auto a = InlineBitmap::FromWord(/*word=*/0x123456789abcdef0,
                                    /*bit_count=*/64, /*fill=*/false);
    auto b = InlineBitmap::FromWord(/*word=*/0x123456789abcdef0,
                                    /*bit_count=*/65, /*fill=*/false);
    // a == b
    EXPECT_EQ(a.UCmp(b), 0);
    EXPECT_EQ(a.UCmp(a), 0);
    EXPECT_EQ(b.UCmp(a), 0);
    EXPECT_EQ(b.UCmp(b), 0);
  }
}

TEST(InlineBitmapTest, Union) {
  {
    InlineBitmap b(0);
    b.Union(InlineBitmap(0));
  }

  {
    InlineBitmap b(1);
    EXPECT_FALSE(b.Get(0));
    b.Union(InlineBitmap(1));
    EXPECT_FALSE(b.Get(0));
    b.Union(InlineBitmap::FromWord(1, 1));
    EXPECT_TRUE(b.Get(0));
  }

  {
    InlineBitmap b(2);
    EXPECT_FALSE(b.Get(0));
    b.Union(InlineBitmap(2));
    EXPECT_FALSE(b.Get(0));
    EXPECT_FALSE(b.Get(1));
    b.Union(InlineBitmap::FromWord(2, 2));
    EXPECT_FALSE(b.Get(0));
    EXPECT_TRUE(b.Get(1));
  }

  {
    InlineBitmap b = InlineBitmap::FromWord(0b00001100, 8);
    b.Union((InlineBitmap::FromWord(0b10001001, 8)));
    EXPECT_EQ(b.GetWord(0), 0b10001101);
    b.Union((InlineBitmap::FromWord(0b11111111, 8)));
    EXPECT_EQ(b.GetWord(0), 0b11111111);
  }

  {
    InlineBitmap b1(80);
    b1.SetByte(0, 0xab);
    b1.SetByte(1, 0xcd);
    b1.SetByte(2, 0xa5);
    b1.SetByte(9, 0x84);

    InlineBitmap b2(80);
    b2.SetByte(0, 0xfb);
    b2.SetByte(1, 0xee);
    b2.SetByte(5, 0x42);
    b2.SetByte(9, 0x31);

    b1.Union(b2);
    EXPECT_EQ(b1.GetByte(0), 0xfb);
    EXPECT_EQ(b1.GetByte(1), 0xef);
    EXPECT_EQ(b1.GetByte(2), 0xa5);
    EXPECT_EQ(b1.GetByte(3), 0);
    EXPECT_EQ(b1.GetByte(4), 0);
    EXPECT_EQ(b1.GetByte(5), 0x42);
    EXPECT_EQ(b1.GetByte(6), 0);
    EXPECT_EQ(b1.GetByte(7), 0);
    EXPECT_EQ(b1.GetByte(8), 0);
    EXPECT_EQ(b1.GetByte(9), 0xb5);
  }
}

}  // namespace

// Note: tests below this point are friended, so cannot live in the anonymous
// namespace.

TEST(InlineBitmapTest, MaskForWord) {
  EXPECT_EQ(InlineBitmap(/*bit_count=*/8).MaskForWord(0), 0xff);
  EXPECT_EQ(InlineBitmap(/*bit_count=*/16).MaskForWord(0), 0xffff);
  constexpr uint64_t kUint64Max = std::numeric_limits<uint64_t>::max();
  EXPECT_EQ(InlineBitmap(/*bit_count=*/63).MaskForWord(0), kUint64Max >> 1);
  EXPECT_EQ(InlineBitmap(/*bit_count=*/64).MaskForWord(0), kUint64Max);

  EXPECT_EQ(InlineBitmap(/*bit_count=*/65).MaskForWord(0), kUint64Max);
  EXPECT_EQ(InlineBitmap(/*bit_count=*/65).MaskForWord(1), 0x1);
  EXPECT_EQ(InlineBitmap(/*bit_count=*/66).MaskForWord(1), 0x3);
  EXPECT_EQ(InlineBitmap(/*bit_count=*/127).MaskForWord(1), kUint64Max >> 1);
  EXPECT_EQ(InlineBitmap(/*bit_count=*/128).MaskForWord(1), kUint64Max);

  // Check that putting a "1" in the least significant bit of b8 shows up, given
  // the mask that is created.
  {
    auto b = InlineBitmap::FromBytes(
        65, {0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8 | 1});
    EXPECT_EQ(b.MaskForWord(1), 0x1);
    EXPECT_EQ(b.GetWord(0), 0xb7'b6'b5'b4'b3'b2'b1'b0);
    EXPECT_EQ(b.GetWord(1), 0x1);
  }

  // Check a single sub-word set of bytes just for fun.
  //
  // Observe that b0 becomes the least significant byte because we copy it into
  // a little endian word.
  {
    auto b = InlineBitmap::FromBytes(9, {0xff, 0xcd});
    EXPECT_EQ(b.MaskForWord(0), 0x01ff);
    EXPECT_EQ(b.GetWord(0), 0x01'ff);
  }
}

}  // namespace xls
