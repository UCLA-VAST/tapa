"""Main package for TAPA CLI."""

__copyright__ = """
Copyright (c) 2024 RapidStream Design Automation, Inc. and contributors.
All rights reserved. The contributor(s) of this file has/have agreed to the
RapidStream Contributor License Agreement.
"""

import os.path

with open(os.path.join(os.path.dirname(__file__), "VERSION"), encoding="utf-8") as _fp:
    __version__ = _fp.read().strip()
