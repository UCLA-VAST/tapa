import collections
import copy
from typing import Dict, Iterable, Optional, Tuple, Union

from tapa.verilog import ast
from tapa.verilog.xilinx.typing import IOPort

__all__ = [
    'M_AXIS_PREFIX', 'S_AXIS_PREFIX',
    'AXIS_PORT_WIDTHS',
    'M_AXIS_PORTS', 'S_AXIS_PORTS',
    'AXIS_PORTS',
    'AXIS_SUFFIXES',
    'M_AXIS_PARAM_PREFIX', 'S_AXIS_PARAM_PREFIX',
    'AXIS_PARAM_SUFFIXES',
    'M_AXIS_PARAMS', 'S_AXIS_PARAMS',
    'is_m_axis_port', 'is_s_axis_port', 'is_axis_port',
    'is_m_axis_param', 'is_s_axis_param', 'is_axis_param',
    'is_m_axis_unique_param', 'is_s_axis_unique_param',
    'rename_axis_name',
    'rename_axis_port',
    'rename_axis_param',
    'get_axis_port_width',
]

M_AXIS_PREFIX = 'm_axis_'
S_AXIS_PREFIX = 's_axis_'

# width=0 means configurable
AXIS_PORT_WIDTHS = dict(
    DATA=0,
    DEST=1,
    ID=1,
    KEEP=0,
    LAST=1,
    READY=1,
    STRB=0,
    USER=1,
    VALID=1,
)

# [(name, direction), ...]
M_AXIS_PORTS = (
    ('DATA', 'output'),
    ('DEST', 'output'),
    ('ID',   'output'),
    ('KEEP', 'output'),
    ('LAST', 'output'),
    ('READY','input'),
    ('STRB', 'output'),
    ('USER', 'output'),
    ('VALID','output'),
)
S_AXIS_PORTS = (
    ('DATA', 'input'),
    ('DEST', 'input'),
    ('ID',   'input'),
    ('KEEP', 'input'),
    ('LAST', 'input'),
    ('READY','output'),
    ('STRB', 'input'),
    ('USER', 'input'),
    ('VALID','input'),
)

# {channel: [(name, direction), ...]}
AXIS_PORTS: Dict[str, Tuple[Tuple[str, str], ...]] = collections.OrderedDict(
    M=M_AXIS_PORTS,
    S=S_AXIS_PORTS,
)

AXIS_SUFFIXES = (
    '_TDATA',
    '_TDEST',
    '_TID',
    '_TKEEP',
    '_TLAST',
    '_TREADY',
    '_TSTRB',
    '_TUSER',
    '_TVALID',
)

M_AXIS_PARAM_PREFIX = 'C_M_AXIS_'
S_AXIS_PARAM_PREFIX = 'C_S_AXIS_'

AXIS_PARAM_SUFFIXES = (
    '_ID_WIDTH',
    '_DATA_WIDTH',
    '_DEST_WIDTH',
    '_KEEP_WIDTH',
    '_STRB_WIDTH',
    '_USER_WIDTH',
)

M_AXIS_PARAMS = ('C_M_AXIS_DATA_WIDTH', 'C_M_AXIS_STRB_WIDTH', 'C_M_AXIS_KEEP_WIDTH')
S_AXIS_PARAMS = ('C_S_AXIS_DATA_WIDTH', 'C_S_AXIS_STRB_WIDTH', 'C_S_AXIS_KEEP_WIDTH')

def is_axis_port(prefix: str, port: Union[str, IOPort]) -> bool:
  if not isinstance(port, str):
    port = port.name
  return (port.startswith(prefix) and
          '_' + port.split('_')[-1] in AXIS_SUFFIXES)

def is_m_axis_port(port: Union[str, IOPort]) -> bool:
  return is_axis_port(M_AXIS_PREFIX, port)

def is_s_axis_port(port: Union[str, IOPort]) -> bool:
  return is_axis_port(S_AXIS_PREFIX, port)

def if_axis_param(prefix: str, param: Union[str, ast.Parameter]) -> bool:
  if not isinstance(param, str):
    param = param.name
  param_split = param.split('_')
  return (len(param_split) > 5 and param.startswith(prefix) and
          ''.join(map('_{}'.format, param_split[-2:])) in AXIS_PARAM_SUFFIXES)

def is_m_axis_param(param: Union[str, ast.Parameter]) -> bool:
  return is_axis_param(M_AXIS_PARAM_PREFIX, port)

def is_s_axis_param(param: Union[str, ast.Parameter]) -> bool:
  return is_axis_param(S_AXIS_PARAM_PREFIX, port)

def is_s_axis_port(port: Union[str, IOPort]) -> bool:
  return is_axis_port(S_AXIS_PREFIX, port)

def is_m_axi_unique_param(param: Union[str, ast.Parameter]) -> bool:
  if not isinstance(param, str):
    param = param.name
  return param in M_AXIS_PARAMS

def is_s_axi_unique_param(param: Union[str, ast.Parameter]) -> bool:
  if not isinstance(param, str):
    param = param.name
  return param in S_AXIS_PARAMS


def rename_axis_name(mapping: Dict[str, str], name: str, idx1: int,
                      idx2: int) -> str:
  try:
    name_snippets = name.split('_')
    return '_'.join(name_snippets[:idx1] +
                    [mapping['_'.join(name_snippets[idx1:idx2])]] +
                    name_snippets[idx2:])
  except KeyError:
    pass
  raise ValueError("'%s' is a result of renaming done by Vivado HLS; " %
                   '_'.join(name_snippets[idx1:idx2]) +
                   'please use a different variable name')

def rename_axis_port(
    mapping: Dict[str, str],
    port: IOPort,
) -> IOPort:
  new_port = copy.copy(port)
  new_port.name = rename_axis_name(mapping, port.name, 2, -1)
  if port.width is not None and isinstance(port.width.msb, ast.Minus):
    new_port.width = copy.copy(new_port.width)
    new_port.width.msb = copy.copy(new_port.width.msb)
    new_port.width.msb.left = ast.Identifier(
        rename_axis_name(mapping, port.width.msb.left.name, 3, -2))
  return new_port


def rename_axis_param(mapping: Dict[str, str],
                       param: ast.Parameter) -> ast.Parameter:
  new_param = copy.copy(param)
  new_param.name = rename_axis_name(mapping, param.name, 3, -2)
  return new_param


def get_axis_port_width(
    port: str,
    data_width: int,
    id_width: Optional[int] = None,
    user_width: Optional[int] = None,
    dest_width: Optional[int] = None,
    vec_ports: Iterable[str] = ('ID','USER','DEST',),
) -> Optional[ast.Width]:
  width = AXIS_PORT_WIDTHS[port]
  if width == 0:
    if port == 'DATA':
      width = data_width
    elif port == 'KEEP' or port == 'STRB':
      width = data_width // 8
  elif width == 1 and port not in vec_ports:
    return None
  if port == 'ID' and id_width is not None:
    width = id_width
  if port == 'USER' and user_width is not None:
    width = user_width
  if port == 'DEST' and dest_width is not None:
    width = dest_width
  return ast.Width(msb=ast.Constant(width - 1), lsb=ast.Constant(0))
