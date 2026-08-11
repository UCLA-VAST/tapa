__copyright__ = """
Copyright (c) 2024 RapidStream Design Automation, Inc. and contributors.
All rights reserved. The contributor(s) of this file has/have agreed to the
RapidStream Contributor License Agreement.
"""

import os
import os.path
import subprocess
import sys

project = "TAPA"
author = "TAPA contributors"
copyright = "2024, " + author  # noqa: A001

html_theme = "sphinx_rtd_theme"

extensions = [
    "breathe",  # Handles Doxygen output.
    "myst_parser",  # Handles MarkDown
    "sphinx_click",  # Handles click-based applications.
    "sphinx.ext.autodoc",  # Handles Python docstring.
    "sphinx.ext.autosectionlabel",  # Creates labels automatically.
    "sphinx.ext.napoleon",  # Handles Google-stype docstring.
]

breathe_default_project = "TAPA"

if os.environ.get("READTHEDOCS") == "True":
    docs_dir = os.path.dirname(__file__)
    build_dir = f"{docs_dir}/../build/docs"
    doxyfile_out = f"{build_dir}/Doxyfile"
    doxygen_dir = f"{build_dir}/doxygen"
    os.makedirs(build_dir, exist_ok=True)
    with (
        open(f"{docs_dir}/Doxyfile.in", encoding="utf-8") as doxyfile_in_fp,
        open(doxyfile_out, "w", encoding="utf-8") as doxyfile_out_fp,
    ):
        for line in doxyfile_in_fp:
            doxyfile_out_fp.write(
                line.replace("@DOXYGEN_OUTPUT_DIR@", doxygen_dir).replace(
                    "@DOXYGEN_INPUT_DIR@", f"{docs_dir}/.."
                )
            )
    subprocess.call(["doxygen", doxyfile_out])
    breathe_projects = {breathe_default_project: f"{doxygen_dir}/xml"}

# Make sure to use the tapa package shipped with the documentation.
sys.path.insert(0, os.path.dirname(__file__) + "/..")

# Make sure the target is unique.
autosectionlabel_prefix_document = True

# Generate labels for #, ##, and ### in markdown.
myst_heading_anchors = 3

source_suffix = [".rst", ".md"]
