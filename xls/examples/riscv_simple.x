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

// A trivial implementation of a simple RISC-V, supporting only
// a subset of the RV32I operations, as described here:
// https://content.riscv.org/wp-content/uploads/2017/05/riscv-spec-v2.2.pdf
//
// The structure of this program is similar to Chris Leary's
// implementation of the CHStone MIPS Benchmark.
//
// The instruction decode for RV32I is complete.
//
// Only a very small subset of the ISA is implemented here, as this
// way of implementing a core will result in fairly inefficient,
// non-pipelined code.

const REG_COUNT = u32:32;
const DMEM_SIZE = u32:16;  // Bytes

// (Typed) Register shortcuts.
const r0 = u5:0;
const r1 = u5:1;
const r2 = u5:2;
const r3 = u5:3;
const r4 = u5:4;
const r5 = u5:5;
const r6 = u5:6;
const r7 = u5:7;
const r8 = u5:8;
const r9 = u5:9;
const r10 = u5:10;
const r11 = u5:11;
const r12 = u5:12;
const r13 = u5:13;
const r14 = u5:14;
const r15 = u5:15;

// Instruction classes (called Opcode in RISC-V)
const I_LD     = u7:0b0000011;
const I_ARITH  = u7:0b0010011;
const I_JALR   = u7:0b1100111;
const R_CLASS  = u7:0b0110011;
const S_CLASS  = u7:0b0100011;
const B_CLASS  = u7:0b1100011;
const U_CLASS  = u7:0b0110111;
const UJ_CLASS = u7:0b1101111;

// Opcodes.
//
// R-type instructions, where funct7 is also being used.
const ADD = u3:0b000;   // Note: SUB has the same opcode, different funct7.
const SUB = u3:0b000;
const SUB_FUNCT7 = u7:0b0100000;
const SLL = u3:0b001;
const XOR = u3:0b100;
const SRL = u3:0b101;   // Note: SRA has the same opcode, different funct7.
const SRA = u3:0b101;
const SRA_FUNCT7 = u7:0b0100000;
const OR  = u3:0b110;
const AND = u3:0b111;
const LRD = u3:0b011;   // Note: SCD has the same opcode, different funct7.
const SCD = u3:0b011;
const LRD_FUNCT7 = u7:0b000100;
const SCD_FUNCT7 = u7:0b001100;

// I-type instructions.
const LB   = u3:0b000;
const LH   = u3:0b001;
const LW   = u3:0b010;
const LD   = u3:0b011;
const LBU  = u3:0b100;
const LHU  = u3:0b101;
const LWU  = u3:0b110;
const ADDI = u3:0b000;
const SLLI = u3:0b001;
const XORI = u3:0b100;
const SRLI = u3:0b101;
const SRAI = u3:0b101;
const ORI  = u3:0b110;
const ANDI = u3:0b111;
const JALR = u3:0b000;

// S-type instructions.
const SB = u3:0b000;
const SH = u3:0b001;
const SW = u3:0b010;
const SD = u3:0b111;

// SB-type instructions.
const BEQ = u3:0b000;
const BNE = u3:0b001;
const BLT = u3:0b100;
const BGE = u3:0b101;
const BLTU = u3:0b110;
const BGEU = u3:0b111;

// U-type instructions.
const LUI = u3:0;   // determined by opcode itself.

// UJ-type instructions.
const JAL = u3:0;   // determined by opcode itself.

// Get the opcode for an instruction, which is the first step
// needed to determine the instruction format.
fn decode_opcode(ins: u32) -> u7 {
   ins[0 +: u7]
}

// Decoding a 32-bit R-type instruction word into:
//
// 76543210 76543210 76543210 76543210   bits
// -------                               funct7
//        - ----                         rs2
//              ---- -                   rs1
//                    ---                funct3
//                       ---- -          rd
//                             -------   opcode
fn decode_r_instruction(ins: u32) -> (u7, u5, u5, u3, u5, u7) {
   let funct7 = ins[25 +: u7];
   let rs2 =    ins[20 +: u5];
   let rs1 =    ins[15 +: u5];
   let funct3 = ins[12 +: u3];
   let rd =     ins[ 7 +: u5];
   let opcode = ins[ 0 +: u7];
   (funct7, rs2, rs1, funct3, rd, opcode)
}

#[test]
fn decode_r_test_lsb_test() {
   let (funct7, rs2, rs1, funct3, rd, opcode) = decode_r_instruction(
        u32:0b0000001_00001_00001_001_00001_0000001);
   assert_eq(funct7, u7:1);
   assert_eq(rs2, u5:1);
   assert_eq(rs1, u5:1);
   assert_eq(funct3, u3:1);
   assert_eq(rd, u5:1);
   assert_eq(opcode, u7:1);
}

#[test]
fn decode_r_test_msb_test() {
   let (funct7, rs2, rs1, funct3, rd, opcode) = decode_r_instruction(
        u32:0b_1000000_10000_10000_100_10000_1000000);
   assert_eq(funct7, u7:0b1000000);
   assert_eq(rs2, u5:0b10000);
   assert_eq(rs1, u5:0b10000);
   assert_eq(funct3, u3:0b100);
   assert_eq(rd, u5:0b10000);
   assert_eq(opcode, u7:0b1000000);
}

// Decoding a 32-bit I-type instruction word into:
//
// 76543210 76543210 76543210 76543210   bits
// -------- ----                         imm_11_0
//              ---- -                   rs1
//                    ---                funct3
//                       ---- -          rd
//                             -------   opcode
fn decode_i_instruction(ins: u32) -> (u12, u5, u3, u5, u7) {
   let imm_11_0 = ins[20 +: u12];
   let rs1 =      ins[15 +:  u5];
   let funct3 =   ins[12 +:  u3];
   let rd =       ins[ 7 +:  u5];
   let opcode =   ins[ 0 +:  u7];
   (imm_11_0, rs1, funct3, rd, opcode)
}

#[test]
fn decode_i_test_lsb_test() {
   let (imm12, rs1, funct3, rd, opcode) = decode_i_instruction(
        u32:0b000000100001_00001_001_00001_0000001);
   assert_eq(imm12, u12:0x21);  //0b0000 0010 0001
   assert_eq(rs1, u5:1);
   assert_eq(funct3, u3:1);
   assert_eq(rd, u5:1);
   assert_eq(opcode, u7:1);
}

#[test]
fn decode_i_test_msb_test() {
   let (imm12, rs1, funct3, rd, opcode) = decode_i_instruction(
        u32:0b100000010000_10000_100_10000_1000000);
   assert_eq(imm12, u12:0x810);  // 0b1000 0001 0000
   assert_eq(rs1, u5:0b10000);
   assert_eq(funct3, u3:0b100);
   assert_eq(rd, u5:0b10000);
   assert_eq(opcode, u7:0b1000000);
}

// Decoding a 32-bit S-type instruction word into:
//
// 76543210 76543210 76543210 76543210   bits
// -------                               imm_11_5
//        - ----                         rs2
//              ---- -                   rs1
//                    ---                funct3
//                       ---- -          rd
//                             -------   opcode
fn decode_s_instruction(ins: u32) -> (u12, u5, u5, u3, u7) {
   let imm_11_5 = ins[25 +: u7];
   let rs2 =      ins[20 +: u5];
   let rs1 =      ins[15 +: u5];
   let funct3 =   ins[12 +: u3];
   let imm_4_0 =  ins[ 7 +: u5];
   let opcode =   ins[ 0 +: u7];
   (imm_11_5 ++ imm_4_0, rs2, rs1, funct3, opcode)
}

#[test]
fn decode_s_test_lsb_test() {
   let (imm12, rs2, rs1, funct3, opcode) = decode_s_instruction(
        u32:0b0000001_00001_00001_001_00001_0000001);
   assert_eq(imm12, u12:0x21);  //0b0000 0010 0001
   assert_eq(rs2, u5:1);
   assert_eq(rs1, u5:1);
   assert_eq(funct3, u3:1);
   assert_eq(opcode, u7:1);
}

#[test]
fn decode_s_test_msb_test() {
   let (imm12, rs2, rs1, funct3, opcode) = decode_s_instruction(
        u32:0b1000000_10000_10000_100_10000_1000000);
   assert_eq(imm12, u12:0x810);  // 0b1000 0001 0000
   assert_eq(rs2, u5:0b10000);
   assert_eq(rs1, u5:0b10000);
   assert_eq(funct3, u3:0b100);
   assert_eq(opcode, u7:0b1000000);
}


// Decoding a 32-bit U-type instruction word into:
//
// 76543210 76543210 76543210 76543210   bits
// -------- -------- ----                imm_31_12
//                       ---- -          rd
//                             -------   opcode
fn decode_u_instruction(ins: u32) -> (u20, u5, u7) {
   let imm_31_12 = ins[12 +: u20];
   let rd =        ins[ 7 +:  u5];
   let opcode =    ins[ 0 +:  u7];
   (imm_31_12, rd, opcode)
}

#[test]
fn decode_u_test_lsb_test() {
   let (imm20, rd, opcode) = decode_u_instruction(
        u32:0b00000000000000000001_00001_0000001);
   assert_eq(imm20, u20:0x1);
   assert_eq(rd, u5:1);
   assert_eq(opcode, u7:1);
}

#[test]
fn decode_u_test_msb_test() {
   let (imm20, rd, opcode) = decode_u_instruction(
        u32:0b10000000000000000000_10000_1000000);
   assert_eq(imm20, u20:0x80000);
   assert_eq(rd, u5:0b10000);
   assert_eq(opcode, u7:0b1000000);
}


// Decoding a 32-bit B-type instruction word into:
//
// 76543210 76543210 76543210 76543210   bits
// -                                     imm12
//  ------                               imm_10_5
//        - ----                         rs2
//              ---- -                   rs1
//                    ---                funct3
//                       ----            imm_4_1
//                            -          imm_11
//                             -------   opcode
fn decode_b_instruction(ins: u32) -> (u12, u5, u5, u3, u7) {
   let imm_12 =   ins[31 +: u1];
   let imm_10_5 = ins[25 +: u6];
   let rs2 =      ins[20 +: u5];
   let rs1 =      ins[15 +: u5];
   let funct3 =   ins[12 +: u3];
   let imm_4_1 =  ins[ 8 +: u4];
   let imm_11 =   ins[ 7 +: u1];
   let opcode =   ins[ 0 +: u7];
   (imm_12 ++ imm_11 ++ imm_10_5 ++ imm_4_1,
    rs2, rs1, funct3, opcode)
}

#[test]
fn decode_b_test() {
   let (imm12, rs2, rs1, funct3, opcode) = decode_b_instruction(
        u32:0b1_000001_00001_00001_001_0001_1_0000001);
   assert_eq(imm12, (u1:1 ++ u1:1 ++ u6:1 ++ u4:1) as u12);
   assert_eq(rs2, u5:1);
   assert_eq(rs1, u5:1);
   assert_eq(funct3, u3:1);
   assert_eq(opcode, u7:1);
}


// Decoding a 32-bit J-type instruction word into:
//
// 76543210 76543210 76543210 76543210   bits
// -                                     imm20
//  ------- ---                          imm_10_1
//             -                         imm_11
//              ---- ----                imm_19:12
//                       ---- -          rd
//                             -------   opcode
fn decode_j_instruction(ins: u32) -> (u20, u5, u7) {
   let imm_20 =    ins[31 +:  u1];
   let imm_10_1 =  ins[21 +: u10];
   let imm_11 =    ins[20 +:  u1];
   let imm_19_12 = ins[12 +:  u8];
   let rd =        ins[ 7 +:  u5];
   let opcode =    ins[ 0 +:  u7];
   (imm_20 ++ imm_19_12 ++ imm_11 ++ imm_10_1,
    rd, opcode)
}

#[test]
fn decode_j_test() {
   let (imm20, rd, opcode) = decode_j_instruction(
        u32:0b1_0000000001_1_00000001_00001_0000001);
   assert_eq(imm20, (u1:1 ++ u8:1 ++ u1:1 ++ u10:1) as u20);
   assert_eq(rd, u5:1);
   assert_eq(opcode, u7:1);
}

// Run functions for the various instruction types.
// ==================================================

// R-Type instructions.
fn run_r_instruction(pc: u32,
                     ins: u32,
                     regs: u32[REG_COUNT],
                     dmem: u8[DMEM_SIZE])
                 -> (u32, u32[REG_COUNT], u8[DMEM_SIZE]) {
  let (funct7, rs2, rs1, funct3, rd, opcode) = decode_r_instruction(ins);
  let new_value = match funct3 {
     XOR => regs[rs1]  ^ regs[rs2],
     AND => regs[rs1]  & regs[rs2],
     OR  => regs[rs1]  | regs[rs2],
     SLL => regs[rs1] << regs[rs2],
     // Note: ADD and SUB have the same opcode
     ADD => match funct7 {
        ADD_FUNCT7 => regs[rs1] + regs[rs2],
        SUB_FUNCT7 => regs[rs1] - regs[rs2],
        _          => fail!("unknown_ADD_funct7", u32:0),
        },
     // Note: SRL and SRA have the same opcode
     SRL => match funct7 {
        SRL_FUNCT7 => regs[rs1] >> regs[rs2],
        SRA_FUNCT7 => ((regs[rs1] as s32) >> regs[rs2]) as u32,
        _          => fail!("unknown_SRL_funct7", u32:0),
        },
     // LD.R, ST.C (atomics) will not be implemented here.
     _   => fail!("unmatched_func3", u32:0)
  };
  (pc + u32:4, update(regs, rd, new_value), dmem)
}


// I-type instructions.
fn run_i_instruction(pc: u32,
                     ins: u32,
                     regs: u32[REG_COUNT],
                     dmem: u8[DMEM_SIZE])
                     -> (u32, u32[REG_COUNT], u8[DMEM_SIZE]) {
  let (imm12, rs1, funct3, rd, opcode) = decode_i_instruction(ins);
  let (pc, new_value) = match opcode {
     I_ARITH => {
        let pc: u32 = pc + u32:4;
        let value: u32 = match funct3 {
           ADDI => regs[rs1] +   (imm12 as u32),
           SLLI => regs[rs1] <<  (imm12 as u32),
           XORI => regs[rs1] ^   (imm12 as u32),
           SRLI => regs[rs1] >>  (imm12 as u32),
           SRLA => ((regs[rs1] as s32) >> (imm12 as u32)) as u32,
           ORI  => regs[rs1] |   (imm12 as u32),
           ANDI => regs[rs1] &   (imm12 as u32),
           _    => fail!("unmatched_I_ARITH_func3", u32:0)
        };
        (pc, value)
     },
     I_LD => {
        let pc: u32 = pc + u32:4;
        let addr:u32 = regs[rs1] + (imm12 as u32);
        let value: u32 = match funct3 {
           LB   => signex(dmem[addr], u32:0),
           LH   => signex(dmem[addr] ++ dmem[addr + u32:1], u32:0),
           LW   => dmem[addr + u32:0] ++
                   dmem[addr + u32:1] ++
                   dmem[addr + u32:2] ++
                   dmem[addr + u32:3],

           LBU  => dmem[regs[addr]] as u32,
           LHU  => (dmem[addr] ++ dmem[addr + u32:1]) as u32,
           _    => fail!("unmatched_I_LD_func3", u32:0)
        };
        (pc, value)
     },
     I_JALR => {
        let (pc, value) = match funct3 {
          JALR => {let new_rd : u32 = pc + u32:4;
                  // Add imm12 to rs1 and clear the LSB
                  let pc = (regs[rs1] + signex(imm12, u32:0)) & u32:0xfffffffe;
                  (pc, new_rd) },
          _    => { fail!("unmatched_I_JALR_funct3", (pc, u32:0)) }
        };
        (pc, value)
     },

     // Unsupported RV64I instructions:
     //   LD, LWU
     _    => { fail!("unsupported_instruction", (pc, u32:0)) }
  };
  (pc, update(regs, rd, new_value), dmem)
}


// S-type instructions.
fn run_s_instruction(pc: u32,
                     ins: u32,
                     regs: u32[REG_COUNT],
                     dmem: u8[DMEM_SIZE])
                     -> (u32, u32[REG_COUNT], u8[DMEM_SIZE]) {
  let (imm12, rs2, rs1, funct3, opcode) = decode_s_instruction(ins);

  // Store various byte length to dmem.
  // This is where are much smarter load/store queue mechanism
  // will end up, which will resolve issues around alignment as well.
  let dmem = match funct3 {
     SW   => {
       let dmem = update(dmem, regs[rs2] + u32:0 + (imm12 as u32),
                               regs[rs1][24 +: u8]);
             let dmem = update(dmem, regs[rs2] + u32:1 + (imm12 as u32),
                               regs[rs1][16 +: u8]);
             let dmem = update(dmem, regs[rs2] + u32:2 + (imm12 as u32),
                               regs[rs1][8 +: u8]);
                        update(dmem, regs[rs2] + u32:3 + (imm12 as u32),
                               regs[rs1][0 +: u8])
     },
     SH   => {
         let dmem = update(dmem, regs[rs2] + u32:0 + (imm12 as u32),
                               regs[rs1][8 +: u8]);
                        update(dmem, regs[rs2] + u32:1 + (imm12 as u32),
                               regs[rs1][0 +: u8])
     },
     SB   => {           update(dmem, regs[rs2] + u32:0 + (imm12 as u32),
                               regs[rs1][0 +: u8])
    },
     // Note: SD is a RV64I-only instruction.
      _   => { fail!("unsupported_funct3", dmem) },
  };
  (pc + u32:4, regs, dmem)
}

// U-type instructions, of which there
// is only one: the LUI instruction.
fn run_u_instruction(pc: u32,
                     ins: u32,
                     regs: u32[REG_COUNT],
                     dmem: u8[DMEM_SIZE])
                     -> (u32, u32[REG_COUNT], u8[DMEM_SIZE]) {
  let (imm20, rdest, opcode) = decode_u_instruction(ins);
  (pc + u32:4, update(regs, rdest, (imm20 as u32) << u32:12), dmem)
}

// UJ-type instructions, of which there
// is only one: the JAL instruction.
fn run_uj_instruction(pc: u32,
                      ins: u32,
                      regs: u32[REG_COUNT],
                      dmem: u8[DMEM_SIZE])
                      -> (u32, u32[REG_COUNT], u8[DMEM_SIZE]) {
   let (imm20, rdest, opcode) = decode_j_instruction(ins);
   (pc + signex(imm20 ++ u1:0, u32:0), update(regs, rdest, pc + u32:4), dmem)
}


// B-Type instructions.
fn run_b_instruction(pc: u32,
                     ins: u32,
                     regs: u32[REG_COUNT],
                     dmem: u8[DMEM_SIZE])
                 -> (u32, u32[REG_COUNT], u8[DMEM_SIZE]) {
  let (imm12, rs2, rs1, funct3, opcode) = decode_b_instruction(ins);
  let pc4 = pc + u32:4;
  let pc_imm = pc + signex(imm12 ++ u1:0, u32:0);
  let new_pc = match funct3 {
     BEQ  => if (regs[rs1] as s32) == (regs[rs2] as s32) { pc_imm } else { pc4 },
     BNE  => if (regs[rs1] as s32) != (regs[rs2] as s32) { pc_imm } else { pc4 },
     BLT  => if (regs[rs1] as s32) <  (regs[rs2] as s32) { pc_imm } else { pc4 },
     BGE  => if (regs[rs1] as s32) >= (regs[rs2] as s32) { pc_imm } else { pc4 },
     BLTU => if regs[rs1] < regs[rs2] { pc_imm } else { pc4 },
     BGEU => if regs[rs1] >= regs[rs2] { pc_imm } else { pc4 },
     _    => fail!("unsupported_func3", u32:0)
  };
  (new_pc, regs, dmem)
}


// Run a program by iterating over the instruction memory.
// At this point - only execute a single instruction.
fn run_instruction(pc: u32,
                   ins: u32,
                   regs: u32[REG_COUNT],
                   dmem: u8[DMEM_SIZE]) ->
                   (u32, u32[REG_COUNT], u8[DMEM_SIZE]) {
  let opcode = decode_opcode(ins);
  let (pc, regs, dmem) = match opcode {
      R_CLASS  => run_r_instruction(pc, ins, regs, dmem),
      S_CLASS  => run_s_instruction(pc, ins, regs, dmem),
      I_ARITH  |
      I_JALR   |
      I_LD     => run_i_instruction(pc, ins, regs, dmem),
      B_CLASS  => run_b_instruction(pc, ins, regs, dmem),
      U_CLASS  => run_u_instruction(pc, ins, regs, dmem),
      UJ_CLASS => run_uj_instruction(pc, ins, regs, dmem),
      _ => fail!("unsupported_func3", (pc, regs, dmem)),
    };
    (pc, update(regs, u32:0, u32:0), dmem) // to ensure r0 == 0 at all times.
}

// Make an R-type instruction.
fn make_r_insn(op: u3, rdest: u5, r1: u5, r2: u5) -> u32 {
  let funct7 = match op {
     SUB => SUB_FUNCT7,
     SRA => SRA_FUNCT7,
     LRD => LRD_FUNCT7,
     SCD => SCD_FUNCT7,
     _   => u7:0
  };
  funct7 ++ r2 ++ r1 ++ op ++ rdest ++ R_CLASS
}

// Make an I-type instruction for I_ARITH and I_LD
fn make_i_insn(op: u3, rdest: u5, r1: u5, imm12: u12) -> u32 {
  let itype : u7 = match op {
     ADDI | SLLI | XORI | SRLI | SRAI | ORI | ANDI => I_ARITH,
     _    => I_LD
  };
  imm12 ++ r1 ++ op ++ rdest ++ itype
}

// Make an I-Type instruction for JALR.
fn make_jalr_insn(op: u3, rdest: u5, r1: u5, imm12: u12) -> u32 {
  imm12 ++ r1 ++ op ++ rdest ++ I_JALR
}

// Make an S-type instruction.
fn make_s_insn(op: u3, rs1: u5, rs2: u5, imm12: u12) -> u32 {
   imm12[5 +: u7] ++ rs2 ++ rs1 ++
   op ++ imm12[0 +: u5] ++ S_CLASS
}

// Make a B-type instruction.
fn make_b_insn(op: u3, rs1: u5, rs2: u5, imm12: u12) -> u32 {
      imm12[11 +: u1] ++
      imm12[ 4 +: u6] ++
      rs2 ++
      rs1 ++
      op ++
      imm12[ 0 +: u4] ++
      imm12[10 +: u1] ++
      B_CLASS
}

// Make a U-type instruction.
fn make_u_insn(rdest: u5, imm20: u20) -> u32 {
   imm20 ++ rdest ++ U_CLASS
}


// Make a UJ-type instruction.
fn make_uj_insn(rdest: u5, imm20: u20) -> u32 {
   imm20[19 +:  u1] ++
   imm20[ 0 +: u10] ++
   imm20[11 +:  u1] ++
   imm20[12 +:  u8] ++
   rdest ++ UJ_CLASS
}

// To test all of the above, construct simple instructions
//
// We construct and iterate over the instructions here, in a test(),
// to make sure that above functions are not being inlined multiple
// times.
#[test]
fn risc_v_example_test() {
  // Create an initial machine / process.
  //
  // Create an initial set of registers. All 0's except r1 and r2,
  // which are 1 and 2 correspondingly.
  let regs = u32[REG_COUNT]:[0, 1, 2, 0, ...];

  // Make d-mem as a simple array of 0-initiazlied u32 words.
  let dmem = u8[DMEM_SIZE]:[0, ...];

  // Program counter is a simple register.
  let PC: u32 = u32:0;

  // Run instructions in sequence
  let (PC, regs, dmem) = run_instruction(PC,
                         make_r_insn(XOR, r3, r1, r2), regs, dmem);  // 3
  let (PC, regs, dmem) = run_instruction(PC,
                         make_r_insn(ADD, r6, r3, r3), regs, dmem);  // 6
  let (PC, regs, dmem) = run_instruction(PC,
                         make_s_insn(SW, r6, r0, u12:4), regs, dmem);  // 6
  let (PC, regs, dmem) = run_instruction(PC,
                         make_i_insn(ADDI, r7, r6, u12:1), regs, dmem); // 7
  let (PC, regs, dmem) = run_instruction(PC,
                         make_i_insn(LW, r8, r0, u12:4), regs, dmem); // 6
  let (PC, regs, dmem) = run_instruction(PC,
                         make_r_insn(ADD, r6, r8, r7), regs, dmem);  // 13
  let (PC, regs, dmem) = run_instruction(PC,
                         make_jalr_insn(JALR, r1, r6, u12:128), regs, dmem);
  let (PC, regs, dmem) = run_instruction(PC,
                         make_u_insn(r9, u20:0x80001), regs, dmem);
  let (PC, regs, dmem) = run_instruction(PC,
                         make_uj_insn(r10, u20:8), regs, dmem);

  assert_eq(regs[r6],  u32:13);
  assert_eq(regs[r1],  u32:0x1c);
  assert_eq(PC,        u32:0xa0);
  assert_eq(regs[r9],  u32:0x80001000);
  assert_eq(regs[r10], u32:0x94);

  let (PC, regs, dmem) = run_instruction(PC,
                         make_b_insn(BEQ, r6, r6, u12:8), regs, dmem);
  assert_eq(PC, u32:0xb0);
  let (PC, regs, dmem) = run_instruction(PC,
                         make_b_insn(BNE, r6, r6, u12:8), regs, dmem);
  assert_eq(PC, u32:0xb4);
  let (PC, regs, dmem) = run_instruction(PC,
                         make_b_insn(BGE, r1, r2, u12:8), regs, dmem);
  assert_eq(PC, u32:0xc4);
  let (PC, regs, dmem) = run_instruction(PC,
                         make_b_insn(BLT, r2, r1, s12:-8 as u12), regs, dmem);
  assert_eq(PC, u32:0xb4);
}
