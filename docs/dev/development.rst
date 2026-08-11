Developing TAPA
===============

.. note::

   This section is intended for **developers** who want to contribute to TAPA.
   It explains the development process, the code structure, and the guidelines
   for contributing to the TAPA framework.

Development Environment
-----------------------

TAPA enforces a consistent coding style and provides tools to ensure code
quality. Follow these steps to set up your development environment.

Install Pre-Commit Hooks
~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: bash

   pip install pre-commit
   pre-commit install

.. note::

   The latest version of pre-commit is required, which depends on a newer
   Python version. Some hooks may fail if your Python version is outdated.

Pre-commit hooks run automatically before each commit to ensure code compliance
with style guidelines. To manually run the checks:

.. code-block:: bash

   pre-commit run --all-files

Install Python Dependencies for IDEs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

While Bazel automatically installs required Python dependencies during build and
test, you can manually install them for IDE access:

.. code-block:: bash

   pip install -r tapa/requirements_lock.txt

Setting C++ Compiler Options for IDEs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Generate a ``compile_commands.json`` file to configure your IDE with Bazel's
compiler options:

.. code-block:: bash

   bazel run //:refresh_compile_commands

Code Structure
--------------

The TAPA codebase is organized into several key directories:

- ``bazel/``: Contains Bazel build configurations.

  It defines how the TAPA compiler is used in the Bazel build system, and
  provides additional utilities for building and testing TAPA.

- ``docs/``: Includes documentation files.

  The documentation is written in reStructuredText and built using Sphinx.

- ``fpga-runtime/``: Provides the FPGA runtime library.

  The FPGA runtime library is used to interact with simulator or FPGA based
  on provided bitstream. It uses fast lightweight simulator for cosimulation
  with XO object file, and interacts with XRT library for Vitis simulation or
  on-board testing with XCLBIN file.

- ``tapa-cpp/``: Customizes the Clang C++ preprocessor for TAPA.

  The TAPA C++ preprocessor reprocesses TAPA C++ code before passing to
  ``tapacc`` compiler. It supports TAPA-specific features, such as
  ``[[tapa::pipeline]]`` annotations.

- ``tapa-lib/``: Houses the TAPA runtime library.

  The TAPA runtime library provides core functionality for TAPA tasks,
  streams, and memory maps. It implements platform-specific features
  (e.g., software simulation queues, hardware FIFOs).

- ``tapa-llvm-project/``: Contains the LLVM project with TAPA-specific patches.

  TAPA uses LLVM Clang to generate system interconnect and transformed C++
  code for each task. The LLVM project is customized with TAPA-specific
  features, such as C++ annotations.

- ``tapa-system-includes/``: Creates a custom system include directory for TAPA.

  This Bazel build target collects system include files for ``tapa-cpp``
  and ``tapacc`` compilers. It includes standard C++ headers, TAPA
  dependencies, and TAPA-specific headers for the compilers to run on every OS.

- ``tapa/``: Contains the core TAPA compiler and runtime library.

  The TAPA compiler serves as the entry point for the TAPA framework. It
  invokes ``tapa-cpp`` and ``tapacc`` compilers, synthesizes tasks into RTL
  using HLS tools, and generates system interconnect and XO object file for
  FPGA.

- ``tapacc/``: Implements the TAPA C++ compiler to translate TAPA tasks to JSON.

  The TAPA C++ compiler is a Clang-based compiler for TAPA tasks. It analyzes
  tasks and streams, generating JSON representation of tasks and dataflow.

- ``tests/``: Includes test cases for the TAPA compiler and runtime library.

  The folder includes various TAPA applications. It includes microbenchmarks
  under ``apps/`` for basic functionality testing, and ``regression/`` for
  performance evaluation of TAPA compiled designs

Update Dependencies
-------------------

TAPA depends on several external libraries and tools. This section explains
how to update these dependencies.

General Version Bump Process
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When bumping versions, follow this general workflow:

1. Clear existing lock files.

2. Update dependency declarations.

3. Regenerate lock files.

4. Test the build.

5. Commit changes.

Bazel Dependencies
~~~~~~~~~~~~~~~~~~

For Bazel dependencies:

1. Update the version numbers in ``MODULE.bazel``.

2. Check the `Bazel Central Registry <https://registry.bazel.build/>`_
   for latest versions, and update the ``bazel_dep`` entries in
   ``MODULE.bazel`` accordingly.

3. Remove ``MODULE.bazel.lock`` to force regeneration.

For Python and Node.js toolchains in ``MODULE.bazel``:

.. code-block:: python

   # Update Python version
   python.toolchain(
       python_version = "3.13.2",  # Update version here
       ...
   )
   use_repo(python, python_3_13 = "python_3_13_2")  # Update repo name too

   # Update Python version in pip declaration
   pip.parse(
       python_version = "3.13.2",  # Update version here
       ...
   )

   # Update Node.js version
   node.toolchain(node_version = "17.9.1")

Python Dependencies
~~~~~~~~~~~~~~~~~~~

To update Python packages:

.. code-block:: bash

   # Clear existing lock file
   echo > tapa/requirements_lock.txt

   # Update the dependencies
   bazel run //tapa:requirements.update

This will regenerate the ``requirements_lock.txt`` file with the latest
compatible versions.


XRT Dependency
~~~~~~~~~~~~~~

For XRT (Xilinx Runtime):

1. Check the `XRT GitHub releases <https://github.com/Xilinx/XRT/releases>`_
   for latest versions.

2. Update the version and SHA256 checksum in ``MODULE.bazel``:

   .. code-block:: python

      XRT_VERSION = "202420.2.18.179"  # Update version
      XRT_SHA256 = "..."  # Update SHA256 checksum

3. Calculate SHA256 checksum with:

   .. code-block:: bash

      curl -L https://github.com/Xilinx/XRT/archive/refs/tags/{VERSION}.tar.gz | sha256sum


LLVM Version Updates
~~~~~~~~~~~~~~~~~~~~

To update the LLVM version:

1. Find the latest stable release of LLVM on
   `LLVM GitHub releases <https://github.com/llvm/llvm-project/releases>`_.

2. Update the version numbers in ``MODULE.bazel``:

   .. code-block:: python

      LLVM_VERSION_MAJOR = 20
      LLVM_VERSION_MINOR = 1
      LLVM_VERSION_PATCH = 4

3. Update the SHA256 checksum after downloading the new version:

   .. code-block:: python

      LLVM_SHA256 = "<new_sha256_checksum>"

Pre-commit Hooks
~~~~~~~~~~~~~~~~

Update pre-commit hooks to the latest versions:

.. code-block:: bash

   pre-commit autoupdate

Verifying Updates
~~~~~~~~~~~~~~~~~

After updating dependencies:

1. Remove the lock file: ``rm MODULE.bazel.lock``

2. Run a full build: ``bazel build //...``

3. Run the pre-commit checks: ``pre-commit run --all-files``

4. Commit the changes: ``git commit -a -m "build(deps): bump versions"``

.. note::

   This section provides guidance on updating all types of dependencies in the
   TAPA project, including where to find the latest versions and how to verify
   that the updates work correctly.
