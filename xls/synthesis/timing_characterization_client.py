#
# Copyright 2020 The XLS Authors
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
"""Sample points from a pb to characterize using a synthesis server.

These datapoints can be used in a delay model (where they will be interpolated)
-- the results emitted on stdout are in xls.delay_model.DataPoints prototext
format.
"""

import sys
from typing import Dict, Sequence, Set, Tuple

from absl import flags
from absl import logging

from google.protobuf import text_format
from xls.common import gfile
from xls.common.file.python import filesystem
from xls.delay_model import delay_model_pb2
from xls.delay_model import op_module_generator
from xls.ir.op_specification import OPS
from xls.synthesis import synthesis_pb2
from xls.synthesis import synthesis_service_pb2_grpc

FLAGS = flags.FLAGS
_MIN_FREQ_MHZ = flags.DEFINE_integer(
    'min_freq_mhz', 500, 'Minimum frequency to test.')
_MAX_FREQ_MHZ = flags.DEFINE_integer(
    'max_freq_mhz', 5000, 'Maximum frequency to test.')
_CHECKPOINT_PATH = flags.DEFINE_string(
    'checkpoint_path', '', 'Path at which to load and save checkpoints. ' +
    'Checkpoints will not be kept if unspecified.')
_SAMPLES_PATH = flags.DEFINE_string(
    'samples_path', '', 'Path at which to load samples textproto.')

ENUM2NAME_MAP = dict((op.enum_name, op.name) for op in OPS)


def check_delay_offset(results: delay_model_pb2.DataPoints):
  # find the minimum nonzero delay, presumably from a reg-->reg connection
  minimum_delay = min([x.delay for x in results.data_points if x.delay])
  logging.vlog(0, f'USING DELAY_OFFSET {minimum_delay}')
  for dp in results.data_points:
    dp.delay_offset = minimum_delay


def save_checkpoint(results: delay_model_pb2.DataPoints, checkpoint_path: str):
  if checkpoint_path:
    check_delay_offset(results)
    with gfile.open(checkpoint_path, 'w') as f:
      f.write(text_format.MessageToString(results))


def _search_for_fmax_and_synth(
    stub: synthesis_service_pb2_grpc.SynthesisServiceStub,
    verilog_text: str,
    top_module_name: str,
) -> synthesis_pb2.CompileResponse:
  """Bisects the space of frequencies and sends requests to the server."""
  best_result = synthesis_pb2.CompileResponse()
  high_hz = _MAX_FREQ_MHZ.value * 1e6
  low_hz = _MIN_FREQ_MHZ.value * 1e6
  epsilon_hz = 2 * 1e6

  while (high_hz - low_hz) > epsilon_hz:
    current_hz = (high_hz + low_hz) / 2
    request = synthesis_pb2.CompileRequest()
    request.target_frequency_hz = int(current_hz)
    request.module_text = verilog_text
    request.top_module_name = top_module_name
    logging.vlog(3, '--- Debug')
    logging.vlog(3, high_hz)
    logging.vlog(3, low_hz)
    logging.vlog(3, epsilon_hz)
    logging.vlog(3, current_hz)
    logging.vlog(3, '--- Request')
    logging.vlog(3, request)
    response = stub.Compile(request)
    logging.vlog(3, '--- response')
    logging.vlog(3, response.slack_ps)
    logging.vlog(3, response.max_frequency_hz)
    logging.vlog(3, response.netlist)

    if response.slack_ps >= 0:
      if response.max_frequency_hz:
        logging.info(
            'PASS at %.1fps (slack %dps @min %dps)',
            1e12 / current_hz,
            response.slack_ps,
            1e12 / response.max_frequency_hz,
        )
      else:
        logging.error('PASS but no maximum frequency determined.')
        logging.error('ERROR: this occurs when '
                      'an operator is optimized to a constant.')
        logging.error('Source Verilog:\n%s', request.module_text)
        sys.exit()
      low_hz = current_hz
      if current_hz >= best_result.max_frequency_hz:
        best_result = response
    else:
      if response.max_frequency_hz:
        logging.info(
            'FAIL at %.1fps (slack %dps @min %dps).',
            1e12 / current_hz,
            response.slack_ps,
            1e12 / response.max_frequency_hz,
        )
      else:
        # This shouldn't happen
        logging.info('FAIL but no maximum frequency provided')
      # Speed things up based on response
      if current_hz > (response.max_frequency_hz * 1.5):
        high_hz = response.max_frequency_hz * 1.1
        low_hz = response.max_frequency_hz * 0.9
        new_epsilon_hz = (high_hz - low_hz) / 2
        if new_epsilon_hz < epsilon_hz:
          epsilon_hz = new_epsilon_hz
      else:
        high_hz = current_hz

      # Issue #1002 -- make sure our range contains the response fmax.
      # This is necessary when the "speed up" code above narrows the range,
      # but then Yosys changes its expected fmax to be less than low_hz.
      #  ** This shouldn't happen any more **
      if (not best_result.max_frequency_hz and
          response.max_frequency_hz < low_hz):
        logging.info('PANIC!  Resetting search range.')
        high_hz = response.max_frequency_hz * 1.1
        low_hz = response.max_frequency_hz * 0.89
        current_hz = low_hz

  if best_result.max_frequency_hz:
    logging.info(
        'Done at slack %dps @min %dps.',
        best_result.slack_ps,
        1e12 / best_result.max_frequency_hz,
    )
  else:
    logging.error(
        'INTERNAL ERROR: no passing run.'
    )
    sys.exit()
  return best_result


def _synthesize_ir(stub: synthesis_service_pb2_grpc.SynthesisServiceStub,
                   results: delay_model_pb2.DataPoints,
                   data_points: Dict[str, Set[str]], ir_text: str,
                   op: str, result_bit_count: int,
                   operand_bit_counts: Sequence[int],
                   operand_element_counts: Dict[int, int],
                   specialization: delay_model_pb2.SpecializationKind) -> None:
  """Synthesizes the given IR text and checkpoint resulting data points."""
  if op not in data_points:
    data_points[op] = set()

  bit_count_strs = []
  for bit_count in operand_bit_counts:
    operand = delay_model_pb2.Operation.Operand(
        bit_count=bit_count, element_count=0
    )
    bit_count_strs.append(str(operand))
  key = ', '.join([str(result_bit_count)] + bit_count_strs)
  if specialization:
    key = key + ' ' + str(specialization)
  if key in data_points[op]:
    return
  data_points[op].add(key)

  logging.info('Running %s with %d / %s', op, result_bit_count,
               ', '.join([str(x) for x in operand_bit_counts]))
  module_name = 'top'
  mod_generator_result = op_module_generator.generate_verilog_module(
      module_name, ir_text)

  op_comment = '// op: ' + op + ' \n'
  verilog_text = op_comment + mod_generator_result.verilog_text

  result = _search_for_fmax_and_synth(stub, verilog_text, module_name)
  logging.vlog(3, 'Result: %s', result)

  if result.max_frequency_hz > 0:
    ps = 1e12 / result.max_frequency_hz
  else:
    ps = 0

  # Add a new record to the results proto
  result_dp = results.data_points.add()
  result_dp.operation.op = op
  result_dp.operation.bit_count = result_bit_count
  if specialization:
    result_dp.operation.specialization = specialization
  for bit_count in operand_bit_counts:
    operand = result_dp.operation.operands.add(bit_count=bit_count)
  for opnd_num, element_count in operand_element_counts.items():
    result_dp.operation.operands[opnd_num].element_count = element_count
  result_dp.delay = int(ps)
  # TODO(tcal) currently no support for array result type here

  # Checkpoint after every run.
  save_checkpoint(results, _CHECKPOINT_PATH.value)


def _run_point(
    op_samples: delay_model_pb2.OpSamples,
    point: delay_model_pb2.Parameterization,
    results: delay_model_pb2.DataPoints,
    data_points: Dict[str, Set[str]],
    stub: synthesis_service_pb2_grpc.SynthesisServiceStub,
) -> None:
  """Generate IR and Verilog, run synthesis for one op parameterization."""

  op = op_samples.op
  specialization = op_samples.specialization
  attributes = op_samples.attributes

  op_name = ENUM2NAME_MAP[op]

  # Result type - bitwidth and optionally element count(s)
  res_bit_count = point.result_width
  res_type = f'bits[{res_bit_count}]'
  for res_elem in point.result_element_counts:
    res_type += f'[{res_elem}]'

  # Operand types - bitwidth and optionally element count(s)
  opnd_element_counts: Dict[int, int] = {}
  opnd_types = []
  for bw in point.operand_widths:
    opnd_types.append(f'bits[{bw}]')
  for opnd_elements in point.operand_element_counts:
    tot_elems = 1
    for count in opnd_elements.element_counts:
      opnd_types[opnd_elements.operand_number] += f'[{count}]'
      tot_elems *= count
    opnd_element_counts[opnd_elements.operand_number] = tot_elems

  # Handle attribute string (at most one key/value pair)
  if attributes:
    k, v = attributes.split('=')
    v = v.replace('%r', str(res_bit_count))
    attr = ((k, v),)
  else:
    attr = ()

  # TODO(tcal): complete handling for specialization == HAS_LITERAL_OPERAND
  logging.info('types: %s : %s', res_type, ' '.join(opnd_types))
  literal_operand = None
  repeated_operand = 1 if (
      specialization == delay_model_pb2.OPERANDS_IDENTICAL) else None
  ir_text = op_module_generator.generate_ir_package(
      op_name, res_type, (opnd_types), attr, literal_operand, repeated_operand
  )
  logging.info('ir_text:\n%s\n', ir_text)
  _synthesize_ir(
      stub, results, data_points, ir_text, op, res_bit_count,
      list(point.operand_widths), opnd_element_counts, specialization
  )


def init_data(
    checkpoint_path: str
) -> Tuple[Dict[str, Set[str]], delay_model_pb2.DataPoints]:
  """Return new state, loading data from a checkpoint, if available."""
  data_points = {}
  results = delay_model_pb2.DataPoints()
  if checkpoint_path:
    filesystem.parse_text_proto_file(checkpoint_path, results)
    for data_point in results.data_points:
      op = data_point.operation
      if op.op not in data_points:
        data_points[op.op] = set()
      key = ', '.join([str(op.bit_count)] +
                      [str(x.bit_count) for x in op.operands])
      if op.specialization:
        key = key + ' ' + str(op.specialization)
      data_points[op.op].add(key)
  return data_points, results


def run_characterization(
    stub: synthesis_service_pb2_grpc.SynthesisServiceStub,
) -> None:
  """Run characterization with the given synthesis service."""
  data_points, data_points_proto = init_data(_CHECKPOINT_PATH.value)
  samples_file = _SAMPLES_PATH.value
  op_samples_list = delay_model_pb2.OpSamplesList()
  filesystem.parse_text_proto_file(samples_file, op_samples_list)
  for op_samples in op_samples_list.op_samples:
    for point in op_samples.samples:
      _run_point(op_samples,
                 point,
                 data_points_proto, data_points, stub)

  print('# proto-file: xls/delay_model/delay_model.proto')
  print('# proto-message: xls.delay_model.DataPoints')
  print(data_points_proto)
