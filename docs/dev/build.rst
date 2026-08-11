Building from Source
====================

.. note::

   This guide is for **developers** contributing to or extending TAPA,
   or **advanced users** building TAPA from source for custom OS support.
   For FPGA accelerator development with TAPA, refer to the
   :ref:`User Documentation`.

.. tip::

   If your OS isn't officially supported and you're not a developer,
   consider using a virtual machine or file a
   `feature request on GitHub <https://github.com/tuna/tapa/issues>`_.

System Prerequisites
--------------------

To build TAPA from source, you need:

- `Bazel <https://bazel.build>`_ 7.3.2 or later
- `Binutils <https://www.gnu.org/software/binutils/>`_ 2.30 or later
- `Git <https://git-scm.com>`_
- `Libstdc++ <https://gcc.gnu.org/libstdc++/>`_ matching the most recent GCC
  version installed on your system
- `Python <https://www.python.org>`_ 3.6 or later
- :ref:`Other TAPA dependencies <user/installation:System Prerequisites>`

Install these tools using your OS package manager. For Ubuntu:

.. code-block:: bash

   # Install bazel
   sudo apt-get install apt-transport-https ca-certificates gnupg
   curl -fsSL https://bazel.build/bazel-release.pub.gpg \
     | gpg --dearmor | sudo tee /usr/share/keyrings/bazel-archive-keyring.gpg
   echo "deb [arch=amd64 signed-by=/usr/share/keyrings/bazel-archive-keyring.gpg] \
       https://storage.googleapis.com/bazel-apt stable jdk1.8" \
     | sudo tee /etc/apt/sources.list.d/bazel.list
   sudo apt-get install bazel

   # Install other tools
   sudo apt-get install binutils git python3

.. tip::

   For Bazel installation on other OS, see the
   `Bazel documentation <https://docs.bazel.build/versions/main/install.html>`_.

.. note::

   The `Dockerfile in the TAPA repository <https://github.com/tuna/tapa/blob/main/.github/docker/build-env/Dockerfile.binary-dependencies>`_
   provides a complete build environment. Use it for containerized builds or
   run the Ubuntu commands to install required tools.

Clone the Repository
--------------------

To get started with building TAPA from source, you'll need to clone the repository from GitHub:

.. code-block:: bash

   git clone https://github.com/tuna/tapa.git

If you are contributing to TAPA, fork the repository and clone your fork
instead. When you're ready to contribute, create a new branch for your
changes, commit your work, and open a pull request to contribute your
changes back to the main repository.

Modify the Build Configuration
------------------------------

When building on systems other than a UCLA server, you
will need to modify the ``VARS.bzl`` file in the repository's root directory
to specify the correct Vivado installation paths and versions. The build script
currently assumes default installation paths at
``/opt/tools/xilinx/Vivado/2024.2`` and
``/opt/tools/xilinx/Vivado/2022.2`` for Vivado, and
``/opt/tools/xilinx/Vitis/2024.2`` for Vitis.

If your Xilinx tools are installed in non-standard locations, please modify
the ``XILINX_TOOL_PATH`` variable to reference the correct base installation
directory for your Vivado and Vitis installations. You should also update
``XILINX_TOOL_VERSION`` to specify the version of the latest Xilinx tools
you have installed. With these settings properly configured, the system
will expect your Vivado installation to be located at
``{XILINX_TOOL_PATH}/Vivado/{XILINX_TOOL_VERSION}``.

Furthermore, you should configure ``XILINX_TOOL_LEGACY_VERSION`` to indicate
the earliest version of Xilinx tools installed on your system, along with
``XILINX_TOOL_LEGACY_PATH`` to point to the corresponding installation
directory.

If your system does not have the Xilinx Runtime (XRT) installed, you can
modify the ``HAS_XRT`` variable in the ``VARS.bzl`` file to ``False``. This
will prevent the tests to fail due to the absence of XRT.

Build TAPA from Source
----------------------

To build TAPA, navigate to the root directory of the cloned repository and execute the following command:

.. code-block:: bash

   bazel build //...

This command compiles all TAPA targets, including the compiler, runtime
library, and tests.

For building a specific target, replace ``//...`` with the desired target
name. For instance, to build only the TAPA compiler:

.. code-block:: bash

   bazel build //tapa

.. note::

   To view all available targets, run ``bazel query //...``.

To skip building for the tests, you could use:

.. code-block:: bash

   bazel build //... -- -//tests/...


After the build process completes, you can find the compiled binaries in the
``bazel-bin`` directory. For example, the TAPA compiler binary is located at
``bazel-bin/tapa/tapa``.

.. note::

   The build process duration may vary depending on your system's performance.
   LLVM, a significant dependency used by TAPA for code generation, requires
   considerable time to build. Bazel will cache it after the initial build.

Use the Built TAPA
------------------

.. important::

   Remember to source the Vivado settings script before running the TAPA compiler.

Once TAPA is built, you can use the compiled TAPA compiler to compile your
designs. For example:

.. code-block:: bash

   bazel-bin/tapa/tapa compile \
    -f tests/apps/bandwidth/bandwidth.cpp \
    --cflags -Itests/apps/bandwidth/ \
    -t Bandwidth \
    --clock-period 3 \
    --part-num xcu250-figd2104-2L-e

Remember to rerun the ``bazel build`` command whenever you make changes to the
TAPA compiler or runtime library to ensure you're using the latest version.

Run TAPA Tests
--------------

To run all TAPA tests, including unit tests and integration tests, use the
following command in the repository's root directory:

.. code-block:: bash

   bazel test //...

For running a specific test, replace ``//...`` with the test name. For example,
to test only a specific app:

.. code-block:: bash

   bazel test //tests/apps/vadd:vadd-xosim

Build Binary Distribution
-------------------------

To create a binary distribution of TAPA, navigate to the root directory of the
cloned repository and execute the following command:

.. code-block:: bash

   bazel build --config=release //:tapa-pkg-tar

Find the generated binary distribution in the ``bazel-bin`` directory,
as a tarball named ``tapa-pkg-tar.tar``.

Install the Binary Distribution
-------------------------------

To install the binary distribution, extract the tarball to a directory of your
choice:

.. code-block:: bash

   tar -xvf bazel-bin/tapa-pkg-tar.tar -C /path/to/install

Access the TAPA compiler binary at ``/path/to/install/usr/bin/tapa``.
