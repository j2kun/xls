# Copyright 2021 The XLS Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load(
    "//xls/build_rules:xls_build_defs.bzl",
    "proto_data",
    "xls_ir_opt_ir",
    "xls_ir_verilog",
)
load(
    "//xls/contrib/xlscc/build_rules:xlscc_build_defs.bzl",
    "xls_cc_ir",
)
load("@rules_hdl//verilog:providers.bzl", "verilog_library")

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//xls:xls_internal"],
    licenses = ["notice"],  # Apache 2.0
)

proto_data(
    name = "mux3_block_pb",
    src = "mux3.textproto",
)

proto_data(
    name = "switch_block_pb",
    src = "switch.textproto",
)

proto_data(
    name = "mux1to2_block_pb",
    src = "mux1to2.textproto",
)

xls_cc_ir(
    name = "mux3_ir",
    src = "mux3.cc",
    block = ":mux3_block_pb",
    ir_file = "mux3.ir",
)

xls_cc_ir(
    name = "switch_ir",
    src = "switch.cc",
    block = ":switch_block_pb",
    ir_file = "switch.ir",
)

xls_cc_ir(
    name = "mux1to2_ir",
    src = "mux1to2.cc",
    block = ":mux1to2_block_pb",
    ir_file = "mux1to2.ir",
)

# Read-and-write delay variant.
# The delay block reads and writes to the external memory each proc tick.
xls_cc_ir(
    name = "delay_raw_ir",
    src = "delay_raw.cc",
    block_from_class = "Delay",
    block_pb_out = "delay_raw_block_pb.pbtext",
    ir_file = "delay_raw.ir",
)

# Read-xor-write delay variant.
# The delay block either reads or writes to the external memory each proc tick.
xls_cc_ir(
    name = "delay_rxw_ir",
    src = "delay_rxw.cc",
    block_from_class = "Delay",
    block_pb_out = "delay_rxw_block_pb.pbtext",
    ir_file = "delay_rxw.ir",
)

xls_cc_ir(
    name = "block_from_class_ir",
    src = "block_from_class.cc",
    block_from_class = "MyBlock",
    block_pb_out = "block_from_class.pbtxt",
    ir_file = "block_from_class.ir",
)

xls_ir_opt_ir(
    name = "mux3_opt_ir",
    src = ":mux3.ir",
    opt_ir_file = "mux3_opt_ir.opt.ir",
)

xls_ir_opt_ir(
    name = "switch_opt_ir",
    src = ":switch.ir",
    opt_ir_file = "switch_opt_ir.opt.ir",
)

xls_ir_opt_ir(
    name = "mux1to2_opt_ir",
    src = ":mux1to2.ir",
    opt_ir_file = "mux1to2_opt_ir.opt.ir",
)

xls_ir_opt_ir(
    name = "delay1rw_raw_opt_ir",
    src = ":delay_raw.ir",
    opt_ir_args = {
        "inline_procs": "true",
    },
    opt_ir_file = "delay_raw_1rw.opt.ir",
    ram_rewrites = [
        ":delay_raw_ram1rw_rewrites.textproto",
    ],
)

xls_ir_opt_ir(
    name = "delay1rw_rxw_opt_ir",
    src = ":delay_rxw.ir",
    opt_ir_args = {
        "inline_procs": "true",
    },
    opt_ir_file = "delay_rxw_1rw.opt.ir",
    ram_rewrites = [
        ":delay_rxw_ram1rw_rewrites.textproto",
    ],
)

xls_ir_opt_ir(
    name = "delay_raw_1r1w_opt_ir",
    src = ":delay_raw.ir",
    opt_ir_file = "delay_raw_1r1w.opt.ir",
    ram_rewrites = [
        ":delay_raw_ram1r1w_rewrites.textproto",
    ],
)

xls_ir_opt_ir(
    name = "delay_rxw_1r1w_opt_ir",
    src = ":delay_rxw.ir",
    opt_ir_file = "delay_rxw_1r1w.opt.ir",
    ram_rewrites = [
        ":delay_rxw_ram1r1w_rewrites.textproto",
    ],
)

xls_ir_opt_ir(
    name = "block_from_class_opt_ir",
    src = ":block_from_class.ir",
    opt_ir_file = "block_from_class.opt.ir",
)

xls_ir_verilog(
    name = "mux3_comb_v",
    src = ":mux3_opt_ir.opt.ir",
    codegen_args = {
        "generator": "combinational",
        "module_name": "mux3_comb",
        "top": "Mux3_proc",
    },
    verilog_file = "mux3_comb_v.sv",
)

xls_ir_verilog(
    name = "mux3_stages_5_v",
    src = ":mux3_opt_ir.opt.ir",
    codegen_args = {
        "module_name": "mux3_stages_5",
        "pipeline_stages": "5",
        "generator": "pipeline",
        "input_valid_signal": "input_valid",
        "output_valid_signal": "output_valid",
        "reset": "rst",
        "reset_active_low": "false",
        "reset_asynchronous": "false",
        "reset_data_path": "false",
        "flop_inputs": "false",
        "flop_outputs": "false",
        "top": "Mux3_proc",
    },
    verilog_file = "mux3_stages_5_v.sv",
)

xls_ir_verilog(
    name = "mux1to2_stages_5_v",
    src = ":mux1to2_opt_ir.opt.ir",
    codegen_args = {
        "module_name": "mux1to2_stages_5",
        "pipeline_stages": "5",
        "generator": "pipeline",
        "input_valid_signal": "input_valid",
        "output_valid_signal": "output_valid",
        "reset": "rst",
        "reset_active_low": "false",
        "reset_asynchronous": "false",
        "reset_data_path": "false",
        "flop_inputs": "false",
        "flop_outputs": "false",
        "top": "Mux1To2_proc",
    },
    verilog_file = "mux1to2_stages_5_v.sv",
)

# Read-and-write delay variant with single-port external RAM.
xls_ir_verilog(
    name = "delay_raw_1rw_v",
    src = ":delay_raw_1rw.opt.ir",
    codegen_args = {
        "generator": "pipeline",
        "delay_model": "unit",
        "ram_configurations": "ram:1RW:{req}:{resp}:{wr_comp}".format(
            req = "memory__req",
            resp = "memory__resp",
            wr_comp = "memory__write_completion",
        ),
        "reset": "rst",
        "reset_data_path": "true",
        "reset_active_low": "false",
        "reset_asynchronous": "true",
        "flop_inputs": "false",
        "flop_outputs": "false",
        "streaming_channel_data_suffix": "_data",
        "streaming_channel_ready_suffix": "_ready",
        "streaming_channel_valid_suffix": "_valid",
        "use_system_verilog": "true",
        "pipeline_stages": "2",
    },
    verilog_file = "delay_raw_1rw.sv",
)

xls_ir_verilog(
    name = "delay_rxw_1rw_v",
    src = ":delay_rxw_1rw.opt.ir",
    codegen_args = {
        "generator": "pipeline",
        "delay_model": "unit",
        "ram_configurations": "ram:1RW:{req}:{resp}:{wr_comp}".format(
            req = "memory__req",
            resp = "memory__resp",
            wr_comp = "memory__write_completion",
        ),
        "reset": "rst",
        "reset_data_path": "true",
        "reset_active_low": "false",
        "reset_asynchronous": "true",
        "flop_inputs": "true",
        "flop_inputs_kind": "flop",
        "streaming_channel_data_suffix": "_data",
        "streaming_channel_ready_suffix": "_ready",
        "streaming_channel_valid_suffix": "_valid",
        "use_system_verilog": "true",
        "pipeline_stages": "2",
    },
    verilog_file = "delay_rxw_1rw.sv",
)

xls_ir_verilog(
    name = "delay_raw_1r1w_v",
    src = ":delay_raw_1r1w.opt.ir",
    codegen_args = {
        "generator": "pipeline",
        "delay_model": "unit",
        "ram_configurations": "ram:1R1W:{rd_req}:{rd_resp}:{wr_req}:{wr_comp}".format(
            rd_req = "memory__read_req",
            rd_resp = "memory__read_resp",
            wr_comp = "memory__write_completion",
            wr_req = "memory__write_req",
        ),
        "reset": "rst",
        "reset_data_path": "true",
        "reset_active_low": "false",
        "reset_asynchronous": "true",
        "flop_inputs": "false",
        "streaming_channel_data_suffix": "_data",
        "streaming_channel_ready_suffix": "_ready",
        "streaming_channel_valid_suffix": "_valid",
        "use_system_verilog": "true",
        "pipeline_stages": "2",
    },
    verilog_file = "delay_raw_1r1w.sv",
)

xls_ir_verilog(
    name = "delay_rxw_1r1w_v",
    src = ":delay_rxw_1r1w.opt.ir",
    codegen_args = {
        "generator": "pipeline",
        "delay_model": "unit",
        "ram_configurations": "ram:1R1W:{rd_req}:{rd_resp}:{wr_req}:{wr_comp}".format(
            rd_req = "memory__read_req",
            rd_resp = "memory__read_resp",
            wr_comp = "memory__write_completion",
            wr_req = "memory__write_req",
        ),
        "reset": "rst",
        "reset_data_path": "true",
        "reset_active_low": "false",
        "reset_asynchronous": "true",
        "flop_inputs": "true",
        "flop_inputs_kind": "zerolatency",
        "streaming_channel_data_suffix": "_data",
        "streaming_channel_ready_suffix": "_ready",
        "streaming_channel_valid_suffix": "_valid",
        "use_system_verilog": "true",
        "pipeline_stages": "2",
    },
    verilog_file = "delay_rxw_1r1w.sv",
)

verilog_library(
    name = "delay_raw_1rw",
    srcs = [":delay_raw_1rw.sv"],
    tags = ["XLSCC_RAW_1RW"],
)

verilog_library(
    name = "delay_rxw_1rw",
    srcs = [":delay_rxw_1rw.sv"],
    tags = ["XLSCC_RXW_1RW"],
)

verilog_library(
    name = "delay_raw_1r1w",
    srcs = [":delay_raw_1r1w.sv"],
    tags = ["XLSCC_RAW_1R1W"],
)

verilog_library(
    name = "delay_rxw_1r1w",
    srcs = [":delay_rxw_1r1w.sv"],
    tags = ["XLSCC_RXW_1R1W"],
)

xls_ir_verilog(
    name = "block_from_class_v",
    src = ":block_from_class.opt.ir",
    codegen_args = {
        "module_name": "block_from_class",
        "pipeline_stages": "1",
        "generator": "pipeline",
        "reset": "rst",
        "reset_active_low": "false",
        "reset_asynchronous": "false",
        "reset_data_path": "false",
        "flop_inputs": "false",
        "flop_outputs": "false",
        "top": "MyBlock_proc",
    },
    verilog_file = "block_from_class.sv",
)

cc_test(
    name = "mux3_opt_ir_test",
    srcs = ["mux3_opt_ir_test.cc"],
    data = [":mux3_opt_ir.opt.ir"],
    deps = [
        "//xls/common:xls_gunit_main",
        "//xls/common/file:filesystem",
        "//xls/common/file:get_runfile_path",
        "//xls/common/logging:log_lines",
        "//xls/common/status:matchers",
        "//xls/common/status:ret_check",
        "//xls/common/status:status_macros",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "mux3_v_test",
    srcs = ["mux3_v_test.cc"],
    data = [
        ":mux3_comb_v.sv",
        ":mux3_stages_5_v.sv",
    ],
    deps = [
        "//xls/common:xls_gunit_main",
        "//xls/common/file:filesystem",
        "//xls/common/file:get_runfile_path",
        "//xls/common/logging:log_lines",
        "//xls/common/status:matchers",
        "//xls/common/status:ret_check",
        "//xls/common/status:status_macros",
        "@com_google_googletest//:gtest",
    ],
)
