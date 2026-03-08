Installation
============

.. note::

   This guide walks you through building and installing RapidStream TAPA
   locally from source.

.. note::

   RapidStream is currently unavailable. This section only covers local
   installation of the open-source TAPA compiler.

Local Build Installation
~~~~~~~~~~~~~~~~~~~~~~~~

RapidStream no longer publishes hosted installation artifacts for a one-click
TAPA install. Build the release package locally from this repository, then
install it from the generated tarball.

.. code-block:: bash

  git clone https://github.com/rapidstream-org/rapidstream-tapa.git
  cd rapidstream-tapa

  # Update VARS.bzl first if your Xilinx tools are installed outside
  # the default paths expected by the repository.
  bazel build --config=release //:tapa-pkg-tar

  # Install from the locally built package.
  RAPIDSTREAM_LOCAL_PACKAGE=./bazel-bin/tapa-pkg-tar.tar ./install.sh

Verify the installation by running:

.. code-block:: bash

  tapa --version

System Prerequisites
~~~~~~~~~~~~~~~~~~~~

RapidStream TAPA requires the following dependencies for a local build:

+-------------------+-----------------+----------------------------------------------+
| Dependency        | Version         | Notes                                        |
+===================+=================+==============================================+
| Bazel             | 7.3.2 or newer  | Required to build the release package        |
+-------------------+-----------------+----------------------------------------------+
| Binutils          | 2.30 or newer   | Required to build from source                |
+-------------------+-----------------+----------------------------------------------+
| GNU C++ Compiler  | 7.5.0 or newer  | For simulation and deployment only           |
+-------------------+-----------------+----------------------------------------------+
| Git               | Any recent      | Required to clone the repository             |
+-------------------+-----------------+----------------------------------------------+
| Python            | 3.6 or newer    | Required by the build and Python libraries   |
+-------------------+-----------------+----------------------------------------------+
| Xilinx Vitis      | 2022.1 or newer |                                              |
+-------------------+-----------------+----------------------------------------------+

RapidStream TAPA has been tested on the following operating systems. Use the
appropriate package manager to install the required dependencies if using a
different OS.

Ubuntu / Debian
^^^^^^^^^^^^^^^

.. note::

   For **Ubuntu 18.04 and newer**, or **Debian 10 and newer**. Older versions
   require building TAPA from source.

.. code-block:: bash

  sudo apt-get install g++

RHEL / Amazon Linux
^^^^^^^^^^^^^^^^^^^

.. note::

   For **Red Hat Enterprise Linux 9 and newer**, derivatives like **AlmaLinux
   9 and newer** and **Rocky Linux 9 and newer**, or **Amazon Linux 2023**.
   Older versions require building TAPA from source.

.. code-block:: bash

  sudo yum install gcc-c++ libxcrypt-compat

Fedora
^^^^^^

.. note::

   For **Fedora 34 and newer**. Fedora 39 and newer may have minor issues due
   to system C library changes and Vitis HLS tool incompatibility.

.. code-block:: bash

  sudo yum install gcc-c++ libxcrypt-compat
