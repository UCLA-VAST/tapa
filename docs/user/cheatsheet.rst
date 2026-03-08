Quick Reference
===============

.. note::

   This guide provides a quick reference for using RapidStream TAPA. If you
   are a beginner, start with the
   :ref:`Getting Started <user/getting_started:Getting Started with TAPA>` guide.

Installation
------------

.. code-block:: bash

  git clone https://github.com/rapidstream-org/rapidstream-tapa.git
  cd rapidstream-tapa

  # Update VARS.bzl first if your Xilinx tools are installed outside
  # the default paths expected by the repository.
  bazel build --config=release //:tapa-pkg-tar

  RAPIDSTREAM_LOCAL_PACKAGE=./bazel-bin/tapa-pkg-tar.tar ./install.sh

Compilation with TAPA
---------------------

.. code-block:: bash

  # Host compilation
  tapa g++ -- kernel.cpp host.cpp -o program

  # HLS synthesis
  tapa compile \
    --top TopLevel \
    --platform xilinx_u250_gen3x16_xdma_4_1_202210_1 \
    -f kernel.cpp \
    -o kernel.xo

Executing TAPA Programs
-----------------------

.. code-block:: bash

  # Software simulation
  ./program

  # Hardware simulation
  ./program --bitstream=kernel.xo

  # Vitis emulation
  ./program --bitstream=kernel.hw_emu.xclbin

  # On-board execution
  ./program --bitstream=kernel.hw.xclbin

Debugging TAPA Programs
-----------------------

.. code-block:: bash

  # Log streams
  export TAPA_STREAM_LOG_DIR=/path/to/logs
  ./program

  # View HLS reports
  ls work.out/report/

Optimization with RapidStream
-----------------------------

Only use this section if your environment already includes RapidStream.

.. code-block:: bash

  rapidstream-tapaopt \
    --work-dir ./build \
    --tapa-xo-path kernel.xo \
    --device-config u55c_device.json \
    --floorplan-config floorplan_config.json

TAPA Code Structure
-------------------

Task Definition
~~~~~~~~~~~~~~~

.. code-block:: cpp

  void TaskName(tapa::istream<T>& in,
                tapa::ostream<T>& out,
                tapa::mmap<T> mem,
                uint64_t scalar) {
    // Task logic
  }

Upper-Level Task
~~~~~~~~~~~~~~~~

.. code-block:: cpp

  void TopLevel(tapa::mmap<T> mem, uint64_t scalar) {
    tapa::stream<T> s("stream_name");

    tapa::task()
      .invoke(Task1, s, mem, scalar)
      .invoke(Task2, s, scalar);
  }

Host Invocation
~~~~~~~~~~~~~~~

.. code-block:: cpp

  tapa::invoke(TopLevel,
               bitstream_path,
               tapa::read_only_mmap<const T>(vec),
               tapa::write_only_mmap<T>(out),
               scalar);

Stream Operations
~~~~~~~~~~~~~~~~~

.. code-block:: cpp

  // Read
  T data = stream.read();
  stream >> data;  // Equivalent

  // Write
  stream.write(data);
  stream << data;  // Equivalent

RapidStream Configuration
-------------------------

Generate Virtual Device
~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: python

  from rapidstream import get_u55c_vitis_device_factory
  factory = get_u55c_vitis_device_factory("xilinx_u55c_gen3x16_xdma_3_202210_1")
  factory.generate_virtual_device("u55c_device.json")

Generate Floorplan Configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: python

  from rapidstream import FloorplanConfig
  config = FloorplanConfig(
    port_pre_assignments={".*": "SLOT_X0Y0:SLOT_X0Y0"},
  )
  config.save_to_file("floorplan_config.json")

Custom Device
~~~~~~~~~~~~~

.. code-block:: python

  df = DeviceFactory(row=3, col=2, part_num="xcu55c-fsvh2892-2L-e")

  # Set pblocks
  for x in range(2):
      for y in range(3):
          pblock = f"-add CLOCKREGION_X{x*4}Y{y*4}:CLOCKREGION_X{x*4+3}Y{y*4+3}"
          df.set_slot_pblock(x, y, [pblock])

  # Extract resources
  df.extract_slot_resources()

  # Set capacities
  df.set_slot_capacity(x, y, north=11520)

  # Generate
  df.factory.generate_virtual_device("custom_device.json")

.. note::

   Parameters to be considered for custom device generation are:

   - Grid size: Affects runtime, fragmentation, effectiveness.
   - Slot usage limit: Controls design spread vs concentration.
   - Pre-existing usage: Reserve with ``set_slot_area``/``reduce_slot_area``.
   - Inter-slot routing: Adjust with ``set_slot_capacity``.
