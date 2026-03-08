Getting Started with TAPA
=========================

.. note::

   This guide introduces the basic usage of RapidStream TAPA for creating
   FPGA dataflow accelerators. It assumes you have :ref:`installed TAPA
   <user/installation:Local Build Installation>` and guides you through creating
   a simple vector adder, compiling it for software simulation, synthesizing
   it into RTL, and running hardware simulation using the generated RTL.

We'll cover fundamental concepts and usage of RapidStream TAPA. If you're
migrating from Vitis HLS, see the :ref:`Migrating from Vitis HLS
<tutorial/migrate_from_vitis_hls:migrating from vitis hls>` tutorial.

FPGA TAPA Task
--------------

Let's start with a simple vector addition example using RapidStream TAPA:

.. literalinclude:: ../../tests/apps/vadd/vadd.cpp
   :language: cpp

Find the `complete source code
<https://github.com/rapidstream-org/rapidstream-tapa/blob/main/tests/apps/vadd/vadd.cpp>`_
in the ``vadd.cpp`` file in the ``tests/apps/vadd`` directory of the TAPA
repository. Save it as ``vadd.cpp`` to follow along using command line tools.

This code adds two variable-length ``float`` vectors, ``a`` and ``b``, to
produce a new vector ``c``. It uses four C++ functions: ``Add``,
``Mmap2Stream``, ``Stream2Mmap``, and ``VecAdd``. Each represents a task in
the TAPA dataflow graph. The ``VecAdd`` task instantiates the other three
tasks and defines communication channels between them.

Let's examine each function:

Task ``Add``
~~~~~~~~~~~~

.. code:: cpp

   void Add(tapa::istream<float>& a, tapa::istream<float>& b,
            tapa::ostream<float>& c, uint64_t n) {
     for (uint64_t i = 0; i < n; ++i) {
       c << (a.read() + b.read());
     }
   }

The ``Add`` function takes four arguments:

- :ref:`Input Streams <api istream>`: ``tapa::istream<float>& a``,
  ``tapa::istream<float>& b``
- :ref:`Output Stream <api ostream>`: ``tapa::ostream<float>& c``
- Scalar Parameter: ``uint64_t n``

It performs element-wise addition of two vectors by reading from streams
``a`` and ``b``, adding the elements, and writing the sum to stream ``c``.
The vector size is specified by ``n``.

To read from an input stream:

.. code:: cpp

   a.read() + b.read()

To write to an output stream:

.. code:: cpp

   c << (a.read() + b.read());

.. warning::

   Use ``&`` in the function signature to pass streams by reference. TAPA
   compiler will fail if you pass streams by value.

Tasks ``Mmap2Stream`` and ``Stream2Mmap``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code:: cpp

   void Mmap2Stream(tapa::mmap<const float> mmap, uint64_t n,
                    tapa::ostream<float>& stream) {
     for (uint64_t i = 0; i < n; ++i) {
       stream << mmap[i];
     }
   }

The ``Mmap2Stream`` function reads an input vector from DRAM and writes it
to a stream. It takes three arguments:

- :ref:`Memory-Mapped Interface <api mmap>`: ``tapa::mmap<const float> mmap``
- :ref:`Output Stream <api ostream>`: ``tapa::ostream<stream>& stream``
- Scalar Parameter: ``uint64_t n``

It reads from the memory referenced by ``mmap``:

.. code:: cpp

   mmap[i]

And writes to the stream ``stream``:

.. code:: cpp

   stream << mmap[i];

The ``mmap`` argument is a memory-mapped interface, typically on-board DRAM
for FPGAs. It's accessed like a C++ array ``const float mmap[n]``.

.. warning::

   Pass the ``mmap`` object by value, as it's a pointer to the memory space.

The ``Stream2Mmap`` function performs the reverse operation, reading from the
stream and writing to the memory-mapped interface.

Upper-Level Task ``VecAdd``
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code:: cpp

   void VecAdd(tapa::mmap<const float> a, tapa::mmap<const float> b,
               tapa::mmap<float> c, uint64_t n) {
     tapa::stream<float> a_q("a");
     tapa::stream<float> b_q("b");
     tapa::stream<float> c_q("c");

     tapa::task()
         .invoke(Mmap2Stream, a, n, a_q)
         .invoke(Mmap2Stream, b, n, b_q)
         .invoke(Add, a_q, b_q, c_q, n)
         .invoke(Stream2Mmap, c_q, c, n);
   }

The ``VecAdd`` function instantiates three nested tasks and defines
communication channels between them. It takes four arguments:

- :ref:`Memory-Mapped Interface <api mmap>`: ``tapa::mmap<const float> a``,
  ``b``, and ``tapa::mmap<float> c``
- Scalar Parameter: ``uint64_t n``

.. warning::

   As an *upper-level* task, ``VecAdd`` can only contain task instantiations
   and communication channel definitions in the function body, as shown above.

It defines three :ref:`communication channels <api stream>`: ``a_q``,
``b_q``, and ``c_q`` to connect the child tasks:

.. code:: cpp

   tapa::stream<float> a_q("a");
   tapa::stream<float> b_q("b");
   tapa::stream<float> c_q("c");

To instantiate child tasks, it uses the ``invoke`` method of the
``tapa::task`` object:

.. code:: cpp

   tapa::task().invoke(Mmap2Stream, a, n, a_q) // ...

All four tasks run in parallel. The ``VecAdd`` task waits for all child tasks
to finish before returning.

Host Driver Program
-------------------

To invoke the TAPA task on FPGA, you need a host program. Here's an example
to run the ``VecAdd`` kernel:

.. code-block:: cpp

  void VecAdd(tapa::mmap<const float> a_array, tapa::mmap<const float> b_array,
              tapa::mmap<float> c_array, uint64_t n);

  int main(int argc, char* argv[]) {
    vector<float> a(n);
    vector<float> b(n);
    vector<float> c(n);

    // ...
    tapa::invoke(VecAdd, path_to_bitstream,
                 tapa::read_only_mmap<const float>(a),
                 tapa::read_only_mmap<const float>(b),
                 tapa::write_only_mmap<float>(c),
                 n);
    // ...
  }

Find the
`full host code <https://github.com/rapidstream-org/rapidstream-tapa/blob/main/tests/apps/vadd/vadd-host.cpp>`_
in the ``vadd-host.cpp`` file in the ``tests/apps/vadd`` directory of the
TAPA repository.

.. note::

   Host code must be in a separate file from kernel code. The kernel code is
   compiled into an FPGA bitstream or simulation target, while the host code
   is compiled into a host executable.

.. tip::

   Only one kernel code file can be compiled with TAPA. To split the kernel
   into multiple files, use C++ headers and include them in the main kernel
   file.

Task Invocation
~~~~~~~~~~~~~~~

The ``tapa::invoke`` function starts the top-level task, ``VecAdd``. It
supports software simulation, hardware simulation, and on-board execution
with the same program.

``tapa::invoke`` takes:

1. The top-level kernel function (declared ahead of time)
2. The path to the desired bitstream (empty string for software simulation)
3. The rest of the arguments are passed to the top-level kernel function

Example:

.. code:: cpp

  tapa::invoke(VecAdd, path_to_bitstream,
               tapa::read_only_mmap<const float>(a),
               tapa::read_only_mmap<const float>(b),
               tapa::write_only_mmap<float>(c),
               n);


``a`` and ``b`` are passed as read-only memory-mapped arguments
(``tapa::read_only_mmap``), and ``c`` as ``tapa::write_only_mmap``. Scalar
values like ``n`` are passed directly.

.. note::

  Scalar values are always read-only to the kernel.

Host Compilation
~~~~~~~~~~~~~~~~

To compile the example host code, pass both the kernel and host code to the
TAPA compiler using the ``tapa g++ --`` command:

.. code-block:: bash

   tapa g++ -- vadd.cpp vadd-host.cpp -o vadd

This generates the host executable ``vadd``.

.. note::

   ``tapa g++ --`` is a wrapper around the GNU C++ compiler that includes
   necessary TAPA headers and libraries. It outputs the ``g++`` command
   invoked for reference.

.. note::

   The kernel code file should also be included in the compilation command,
   as it is used for software simulation.

Software Simulation
-------------------

When ``tapa::invoke`` is called with an empty string as the bitstream path,
TAPA will simulate the kernel in software. In the example host driver program,
the bitstream path is passed as a command line argument. To run the software
simulation, execute the host program with no arguments:

.. code-block:: bash

  ./vadd

Output:

.. code-block:: text

  I20000101 00:00:00.000000 0000000 task.h:66] running software simulation with TAPA library
  kernel time: 1.19429 s
  PASS!

The first line indicates that the software simulation is running, instead of
hardware simulation with the RTL or on-board execution on FPGA. Later, we will
use the same executable file for hardware simulation and on-board execution.

The above runs software simulation of the program, which helps you quickly
verify the correctness of your task design.

Synthesis into RTL
------------------

To synthesize the design into RTL:

.. code-block:: bash

  tapa \
    compile \
    --top VecAdd \
    --part-num xcu250-figd2104-2L-e \
    --clock-period 3.33 \
    -f vadd.cpp \
    -o vecadd.xo

This compiles ``vadd.cpp`` into an RTL design named ``vecadd.xo``.
The ``--top`` argument specifies the top-level task to be synthesized.
The ``--part-num`` argument specifies the target FPGA part number.
The ``--clock-period`` argument specifies the target clock period in nanoseconds.

.. note::

   Replace ``--part-num`` and ``--clock-period`` with ``--platform`` to
   specify the target Vitis platform (e.g.,
   ``--platform xilinx_u250_gen3x16_xdma_4_1_202210_1`` for Xilinx U250).

HLS reports will be available in ``work.out/report``.

Hardware Simulation
-------------------

Run hardware simulation using the generated RTL by passing the XO file as the
bitstream path of the ``tapa::invoke`` function. For the vector addition
example host program, use ``--bitstream=vecadd.xo`` to change the argument:

.. code-block:: bash

  ./vadd --bitstream=vecadd.xo 1000

See :ref:`Simulation and RTL Cosimulation
<user/cosim:Simulation and RTL Cosimulation>` for more details.

On-Board Execution
------------------

To generate the Xilinx hardware binary (xclbin) for on-board execution:

.. code-block:: bash

  v++ -o vadd.xilinx_u250_gen3x16_xdma_4_1_202210_1.hw.xclbin \
    --link \
    --target hw \
    --kernel VecAdd \
    --platform xilinx_u250_gen3x16_xdma_4_1_202210_1 \
    vecadd.xo

Hardware binary generation may take several hours. The binary is generated for
the specified platform as ``vadd.xilinx_u250_gen3x16_xdma_4_1_202210_1.hw.xclbin``.

To execute the hardware accelerator on an FPGA:

.. code-block:: bash

  ./vadd --bitstream=vadd.xilinx_u250_gen3x16_xdma_4_1_202210_1.hw.xclbin

See :ref:`Hardware Implementation <user/vitis:Hardware Implementation>` for more details.
