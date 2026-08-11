"""Utility functions for TAPA."""

from __future__ import annotations

__copyright__ = """
Copyright (c) 2024 RapidStream Design Automation, Inc. and contributors.
All rights reserved. The contributor(s) of this file has/have agreed to the
RapidStream Contributor License Agreement.
"""

import getpass
import logging
import os.path
import shutil
import socket
import subprocess
import time
from typing import TYPE_CHECKING

import coloredlogs

if TYPE_CHECKING:
    from collections.abc import Iterable

_logger = logging.getLogger().getChild(__name__)


class Options:
    """Global configuration options."""

    # Only clang-format the first 1MB code to avoid performance bottleneck:
    # https://github.com/tuna/tapa/issues/232
    clang_format_quota_in_bytes: int = 1_000_000


_clang_formatted_byte_count = 0


def clang_format(code: str, *args: str) -> str:
    """Apply clang-format with given arguments, if possible."""
    for version in range(20, 4, -1):
        clang_format_exe = shutil.which(f"clang-format-{version}")
        if clang_format_exe is not None:
            break
    else:
        clang_format_exe = shutil.which("clang-format")
    if clang_format_exe is None:
        return code

    global _clang_formatted_byte_count  # noqa: PLW0603
    new_byte_count = _clang_formatted_byte_count + len(code)
    if new_byte_count > Options.clang_format_quota_in_bytes:
        return code
    _clang_formatted_byte_count = new_byte_count

    _logger.debug("clang-format quota: %d bytes", new_byte_count)
    return subprocess.check_output(
        [clang_format_exe, *args],
        input=code,
        text=True,
    )


def get_indexed_name(name: str, idx: int | None) -> str:
    """Return `name` if `idx` is `None`, `f'{name}_{idx}'` otherwise."""
    if idx is None:
        return name
    return f"{name}_{idx}"


def range_or_none(count: int | None) -> tuple[None] | Iterable[int]:
    if count is None:
        return (None,)
    return range(count)


def get_addr_width(chan_size: int | None, data_width: int) -> int:
    if chan_size is None:
        return 64
    chan_size_in_bytes = chan_size * data_width // 8
    addr_width = (chan_size_in_bytes - 1).bit_length()
    assert 2**addr_width == chan_size * data_width // 8
    return addr_width


def get_instance_name(item: tuple[str, int]) -> str:
    return "_".join(map(str, item))


def get_module_name(module: str) -> str:
    return f"{module}"


def as_type[T](type_: type[T], value: object) -> T:
    if isinstance(value, type_):
        return value
    msg = f"Want {type_}, got {type(value)}"
    raise TypeError(msg)


def as_type_or_none[T](type_: type[T], value: object) -> T | None:
    if value is None:
        return None
    return as_type(type_, value)


def setup_logging(
    verbose: int | None,
    quiet: int | None,
    work_dir: str,
    program_name: str = "tapac",
) -> None:
    verbose = 0 if verbose is None else verbose
    quiet = 0 if quiet is None else quiet
    logging_level = (quiet - verbose) * 10 + logging.INFO
    logging_level = max(logging.DEBUG, min(logging.CRITICAL, logging_level))

    coloredlogs.install(
        level=logging_level,
        fmt="%(levelname).1s%(asctime)s %(name)s:%(lineno)d] %(message)s",
        datefmt="%m%d %H:%M:%S.%f",
    )

    log_dir = os.path.join(work_dir, "log")
    os.makedirs(log_dir, exist_ok=True)

    # The following is copied and modified from absl-py.
    try:
        username = getpass.getuser()
    except KeyError:
        # This can happen, e.g. when running under docker w/o passwd file.
        username = str(os.getuid())
    hostname = socket.gethostname()
    file_prefix = f"{program_name}.{hostname}.{username}.log"
    symlink_prefix = program_name
    time_str = time.strftime("%Y%m%d-%H%M%S", time.localtime(time.time()))
    basename = f"{file_prefix}.INFO.{time_str}.{os.getpid()}"
    filename = os.path.join(log_dir, basename)
    symlink = os.path.join(log_dir, symlink_prefix + ".INFO")
    try:
        if os.path.islink(symlink):
            os.unlink(symlink)
        os.symlink(os.path.basename(filename), symlink)
    except OSError:
        # If it fails, we're sad but it's no error. Commonly, this fails because the
        # symlink was created by another user so we can't modify it.
        pass

    handler = logging.FileHandler(filename, encoding="utf-8")
    handler.setFormatter(
        logging.Formatter(
            fmt=(
                "%(levelname).1s%(asctime)s.%(msecs)03d "
                "%(name)s:%(lineno)d] %(message)s"
            ),
            datefmt="%m%d %H:%M:%S",
        ),
    )
    handler.setLevel(logging.DEBUG)
    logging.getLogger().addHandler(handler)

    _logger.info("logging level set to %s", logging.getLevelName(logging_level))
