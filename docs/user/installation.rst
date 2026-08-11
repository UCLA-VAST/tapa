Installation
============

.. note::

   TAPA is now maintained at `github.com/tuna/tapa
   <https://github.com/tuna/tapa>`_. This repository is the archived
   publication version; please install TAPA from the new repository.

Install from the New Repository
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Install the latest release with the installation script:

.. code-block:: bash

  curl -fsSL https://raw.githubusercontent.com/tuna/tapa/main/install.sh | sh -s -- -q

With root privileges, this installs to ``/opt/tapa`` (symlinks in
``/usr/local/bin``). Without root, it installs to ``~/.tapa`` and updates
your shell ``PATH``. To install a specific version:

.. code-block:: bash

  curl -fsSL https://raw.githubusercontent.com/tuna/tapa/main/install.sh \
    | TAPA_VERSION=<version> sh -s -- -q

See all releases at `github.com/tuna/tapa/releases
<https://github.com/tuna/tapa/releases>`_.

Verify the installation by running:

.. code-block:: bash

  tapa --version

System Prerequisites
~~~~~~~~~~~~~~~~~~~~

Building TAPA from source requires the following dependencies:

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

TAPA has been tested on the following operating systems. Use the
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
