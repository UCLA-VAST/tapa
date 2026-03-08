Getting Started
===============

.. note::

   This part guides you through the basic usage of RapidStream for optimizing
   TAPA FPGA dataflow accelerators. It assumes you have :ref:`installed TAPA
   <user/installation:Local Build Installation>` and are working in an
   environment where RapidStream is already provisioned and have
   generated a TAPA design into an XO file. If you haven't, please refer to
   the :ref:`Getting Started <user/getting_started:Getting Started with TAPA>` guide.

We'll cover the steps of generating a virtual device, configuring the
floorplan process, and running the partition-and-pipeline optimization to
create an optimized TAPA design. We'll also explore how to customize the
target device and control the floorplan process for design space exploration.

To begin optimizing your TAPA design with RapidStream, follow these steps:

Step 1: Generate a Virtual Device
---------------------------------

First, create a virtual device representing your target FPGA. Here's an
example using a pre-defined U55C Vitis device:

.. code-block:: python

   from rapidstream import get_u55c_vitis_device_factory

   # Set the Vitis platform name
   factory = get_u55c_vitis_device_factory("xilinx_u55c_gen3x16_xdma_3_202210_1")
   # Generate the virtual device in JSON format
   factory.generate_virtual_device("u55c_device.json")

.. note::

   To create a virtual device for your own FPGA, you can customize the
   ``DeviceFactory`` object. We'll cover this in more detail later.

Save this code to a file named ``gen_device.py`` and run it using the
``rapidstream`` executable:

.. code-block:: bash

   rapidstream gen_device.py

.. note::

   The ``rapidstream`` executable is not publicly distributed with TAPA.
   These steps only apply in environments where RapidStream is already
   available.

Step 2: Configure the Floorplanning Process
-------------------------------------------

Next, create a configuration file for the floorplanning process. Here's a
simple example that assigns all ports to ``SLOT_X0Y0``:

.. code-block:: python

   from rapidstream import FloorplanConfig

   config = FloorplanConfig(
       port_pre_assignments={".*": "SLOT_X0Y0:SLOT_X0Y0"},
   )
   config.save_to_file("floorplan_config.json")

Save this code to a file named ``gen_floorplan_config.py`` and run it:

.. code-block:: bash

   rapidstream gen_floorplan_config.py

Step 3: Configure the Pipelining Process
----------------------------------------

Then, create another configuration file for the pipelining process. Here's an
example that sets the pipeline scheme to using a single flip-flop per slot
crossing:

.. code-block:: python

   from rapidstream import PipelineConfig

   config = PipelineConfig(
       pp_scheme="single",
   )
   config.save_to_file("pipeline_config.json")

Save this code to a file named ``gen_pipeline_config.py`` and run it:

.. code-block:: bash

   rapidstream gen_pipeline_config.py

Step 4: Run Partition-and-Pipeline Optimization
-----------------------------------------------

Finally, use the ``rapidstream-tapaopt`` executable to perform the
partition-and-pipeline optimization:

.. code-block:: bash

   rapidstream-tapaopt \
       --work-dir ./build \
       --tapa-xo-path [path-to-xo-file] \
       --device-config u55c_device.json \
       --floorplan-config floorplan_config.json \
       --pipeline-config pipeline_config.json

This command generates a new XO file with the optimized design.
