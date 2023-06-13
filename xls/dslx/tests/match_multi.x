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

fn main(x: u32) -> u32 {
  match x {
    u32:24 | u32:42 => u32:42,
    _ => u32:64
  }
}

#[test]
fn main_test() {
  //assert_eq(u32:42, main(u32:24));
  assert_eq(u32:42, main(u32:42));
  //assert_eq(u32:64, main(u32:41));
  ()
}
