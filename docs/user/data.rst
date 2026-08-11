Dataflow Movements
==================

.. note::

  TAPA offers several features for data movements, extending the capabilities
  of Vitis HLS to enhance the flexibility and expressiveness of your designs.

This section covers various aspects of data movement in TAPA, including stream
operations, memory-mapped interfaces, and asynchronous memory access.

Dataflow Channels (Stream)
--------------------------

Streams are the primary data movement mechanism in TAPA. They are used to
transfer data between tasks and modules in a dataflow design. TAPA provides
``stream`` as the instantiation of a single data channel, which is a FIFO
queue with a single reader and writer. ``stream`` variables are supplied as
parameters to tasks to transfer data between them.

.. code-block:: cpp

   tapa::stream<int> channel_1;

TAPA employs distinct ``tapa::istream<T>&`` and ``tapa::ostream<T>&`` classes
for input and output communications. An ``istream`` is used to read data from
a stream, while an ``ostream`` is used to write data to a stream. A task
should specify the direction of data flow for each stream parameter.

This approach visually clarifies interface
directions in the source code, and enhances IDE code completion accuracy,
suggesting only relevant functions (e.g., ``read`` for input streams only):

.. code-block:: cpp

  void Task(tapa::istream<int>& in, tapa::ostream<int>& out) {
    int data = in.read();
    out.write(data);
  }

.. warning::

   The stream formal parameters of a task must be a reference to the stream,
   i.e., ``istream<T>&`` and ``ostream<T>&``, as the stream object is not
   copyable and the parameter should be pointing to the same stream object.

In this example, the ``Task`` function reads an integer from the input stream
``in`` and writes it to the output stream ``out``. To invoke this task, you
can use the ``invoke`` method of the ``task`` object:

.. code-block:: cpp

  void Top() {
    tapa::stream<int> channel_1;
    tapa::stream<int> channel_2;
    tapa::task().invoke(Task, channel_1, channel_2)
    // ... other tasks
  }

Stream Instantiation
^^^^^^^^^^^^^^^^^^^^

Streams are instantiated using the ``stream`` class template. The template
parameter specifies the data type of the stream. For example, to create a
stream of integers, you can use the following code:

.. code-block:: cpp

  tapa::stream<int> channel;

.. tip::

   To avoid deadlocks and boost performance, it is recommended to set the
   depth of the FIFO queue to a value that is large enough to hold the
   in-flight data tokens.

The ``stream`` template could accept an additional template parameter to
specify the depth of the FIFO queue. By default, the depth is set to 2.
In order to specify a different depth, you can use the following code:

.. code-block:: cpp

  tapa::stream<int, 16> channel;

TAPA's software simulation considers the stream depth and blocks the writer
when the stream is full, in contrast to Vitis HLS. This behavior is more
realistic and helps avoid deadlocks in the hardware implementation.

A stream could optionally be named for debugging purposes. The name is used
in the log messages to identify the stream. For example:

.. code-block:: cpp

  tapa::stream<int> channel("channel");

.. note::

   TAPA uses the ``stream`` class template to instantiate streams. The depth
   of the FIFO queue can be specified as an additional template parameter.
   For debugging purposes, a stream can be named.

Stream Read and Write
^^^^^^^^^^^^^^^^^^^^^

Streams provide two primary operations: read and write. The ``read`` operation
reads a token from the stream in a blocking manner, while the ``write``
operation writes a token to the stream. The following code demonstrates the
use of the ``read`` and ``write`` operations:

.. code-block:: cpp

  void Task(tapa::istream<int>& in, tapa::ostream<int>& out) {
    int data = in.read();
    out.write(data);
  }

.. tip::

   A shortcut for reading and writing tokens is to use the ``<<`` and ``>>``
   operators.

To read from multiple streams simultaneously when data is available, achieve
an initiation interval of one, and improve performance, TAPA provides
non-blocking read and write operations. The ``try_read`` and ``try_write``
operations return a boolean value indicating whether the operation was
successful. The following code demonstrates the use of non-blocking read and
write operations:

.. code-block:: cpp

  void Task(tapa::istream<int>& in, tapa::ostream<int>& out) {
    int data;
    bool success = in.try_read(data);
    if (success) {
      out.try_write(data);
    }
  }

.. note::

   The ``read`` and ``write`` operations are used to read from and write to
   streams. TAPA provides non-blocking read and write operations through the
   ``try_read`` and ``try_write`` methods.

Stream Readiness Check
^^^^^^^^^^^^^^^^^^^^^^

TAPA provides an API to check if a stream has data available for reading. This
is useful when you need to make decisions based on the availability of data
in the stream:

.. code-block:: cpp

  void Task(tapa::istream<int>& in, tapa::ostream<int>& out) {
    if (!in.empty()) {
      int data = in.read();
      out.write(data);
    }
  }

For output streams, you can use the ``full()`` method to check if the stream is
full and cannot accept more data:

.. code-block:: cpp

  void Task(tapa::istream<int>& in, tapa::ostream<int>& out) {
    if (!out.full()) {
      int data = in.read();
      out.write(data);
    }
  }

.. note::

   TAPA provides the ``empty()`` method to check if a stream has data available
   for reading, and the ``full()`` method to check if a stream is full and
   cannot accept more data.

Stream Data Peeking
^^^^^^^^^^^^^^^^^^^

TAPA provides non-destructive read (peek) functionality for streams, allowing
you to read a token without removing it. This is useful when computations
depend on the content of input tokens, such as in switch networks.

Example usage from the
`TAPA network app <https://github.com/tuna/tapa/blob/main/tests/apps/network/network.cpp>`_:

.. code-block:: cpp

  for (bool is_valid_0, is_valid_1;;) {
    const auto pkt_0 = pkt_in_q0.peek(valid_0);
    const auto pkt_1 = pkt_in_q1.peek(valid_1);
    // Make decisions based on peeked values
    if (...) pkt_in_q0.read(nullptr);
    if (...) pkt_in_q1.read(nullptr);
  }

This example demonstrates the use of the peeking API through an implementation
of a 3-stage 8×8
`Omega network <https://www.mathcs.emory.edu/~cheung/Courses/355/Syllabus/90-parallel/Omega.html>`_.
At the core of this multi-stage switch network is a 2×2 switch box, which
routes input packets based on a specific bit in their destination address.
This destination address is embedded within the packet itself.

To illustrate, consider a packet from ``pkt_in_q0`` with a destination of 2
(binary ``010``). If we're focusing on bit 1 (using 0-based indexing), this
packet should be directed to ``pkt_out_q[1]``. Similarly, if a packet from
``pkt_in_q1`` has a destination of 7 (binary ``111``), it would also be
routed to ``pkt_out_q[1]``. However, since only one token can be written per
clock cycle, a decision must be made regarding which packet to prioritize.

This decision-making process needs to occur before removing any tokens from
the input channels (streams). The code accomplishes this by first peeking at
the input stream using `peek` to examine the destinations without consuming
the data. Based on these peeked destinations, it determines which inputs can
be processed.

.. note::

   ``.peek()`` returns the token's value and validity, but does not consume
   the token from the stream.

End-of-Transaction
^^^^^^^^^^^^^^^^^^

TAPA allows sending special end-of-transaction (EoT) tokens to denote the end
of a data stream. This is particularly useful in dataflow optimizations where
proper kernel termination is required.

For example, `SODA <https://github.com/UCLA-VAST/soda>`_ is a highly parallel
microarchitecture for stencil applications. It is implemented using
`dataflow optimization <https://www.xilinx.com/html_docs/xilinx2021_1/vitis_doc/vitis_hls_optimization_techniques.html#bmx1539734225930>`_.
However, this approach requires proper termination of each kernel.

Traditionally, this is achieved by broadcasting the loop trip-count to each
kernel function. However, this method necessitates an adder in each function,
which can be resource-intensive, especially for small kernel modules.

TAPA offers a more resource-efficient solution to this problem. It allows
kernels to send a special "End of Transaction" (EoT) token to signify
completion. This approach is demonstrated in the
`jacobi stencil example <https://github.com/tuna/tapa/blob/main/tests/apps/jacobi/jacobi.cpp>`_
provided with TAPA:

The producer, ``Mmap2Stream``, sends an ``EoT`` token by
:ref:`closing <classtapa_1_1ostream_1a10405849fa9a12a02e2fc0d33b305d22>`
the stream by calling ``stream.close()``:

.. code-block:: cpp

  void Mmap2Stream(tapa::mmap<const float> mmap, uint64_t n,
                   tapa::ostream<tapa::vec_t<float, 2>>& stream) {
    [[tapa::pipeline(2)]] for (uint64_t i = 0; i < n; ++i) {
      tapa::vec_t<float, 2> tmp;
      tmp.set(0, mmap[i * 2]);
      tmp.set(1, mmap[i * 2 + 1]);
      stream.write(tmp);
    }
    stream.close();
  }

Downstream modules, such as ``Stream2Mmap`` in this example, can decide on
program termination by checking for the EoT token by checking on the
``eot`` flag returned by ``try_eot()``:

.. code-block:: cpp

  void Stream2Mmap(tapa::istream<tapa::vec_t<float, 2>>& stream,
                   tapa::mmap<float> mmap) {
    [[tapa::pipeline(2)]] for (uint64_t i = 0;;) {
      bool eot;
      if (stream.try_eot(eot)) {
        if (eot) break;
        auto packed = stream.read(nullptr);
        mmap[i * 2] = packed[0];
        mmap[i * 2 + 1] = packed[1];
        ++i;
      }
    }
  }

In summary, the API for EoT tokens in TAPA is as follows:

.. code-block:: cpp

  // Producer
  stream.close();

  // Consumer
  bool eot;
  if (stream.try_eot(eot)) {
    if (eot) break;
    // Process data
  }

.. tip::

   The downstream could reopen the stream by calling ``stream.open()`` after
   the EoT token is detected. In this way, the stream can be reused for
   multiple transactions.

.. note::

   TAPA supports the ``close()`` and ``try_eot()`` APIs to close a stream and
   check for the EoT token, respectively.

Memory-Mapped (MMAP)
--------------------

Memory-mapped interfaces are used to access external memory in TAPA. They
provide a simple and efficient way to read and write data to and from memory
in a dataflow design. TAPA provides the ``mmap`` class template to represent
memory-mapped interfaces.

.. code-block:: cpp

  void Task(tapa::mmap<const int> mem) {
    int data = mem[0];
  }

In this example, the ``Task`` function reads an integer from the memory-mapped
interface ``mem``. ``tapa::mmap`` can only be supplied as a parameter to tasks
and cannot be used as a local variable, as it represents an external memory
interface.

.. warning::

   The memory-mapped interface formal parameters of a task must be passed by
   value, i.e., ``mmap<T>``. Passing by reference is not allowed.

.. note::

   TAPA provides the ``mmap`` class template to represent memory-mapped
   memory interfaces, passed by value as formal parameters to tasks.

MMAP Instantiation
^^^^^^^^^^^^^^^^^^

Memory spaces could be allocated on the stack or heap on the host side and
passed to the FPGA kernel as arguments. For example, to create a memory-mapped
space of integers, you can use the following code:

.. code-block:: cpp

  std::vector<int> vec(16);

However, if the allocated memory space is not aligned to page boundaries, an
extra memory copy is required for host-kernel communication. To resolve this
issue and eliminate the extra copy, you can use a specialized vector with
aligned memory allocation:

.. code-block:: cpp

  std::vector<int, tapa::aligned_allocator<int>> vec(16);

.. note::

   TAPA maps host memory to FPGA memory using memory-mapped interfaces by
   passing the memory space as arguments to the FPGA kernel.

MMAP Argument Passing
^^^^^^^^^^^^^^^^^^^^^

The top-level task can be invoked with memory-mapped interfaces as arguments.
The direction of data flow should be specified in the task invocation:

.. code:: cpp

  tapa::invoke(Task, path_to_bitstream,
               tapa::read_only_mmap<int>(vec));

Similarly, write-only memory-mapped interfaces can be passed to the task as
``tapa::write_only_mmap``, and read-write memory-mapped interfaces can be
passed as ``tapa::read_write_mmap``.

.. warning::

  ``tapa::read_only_mmap`` and ``tapa::write_only_mmap`` only specify
  host-kernel communication behavior, not kernel access patterns.

For passing memory-mapped interfaces to nested tasks, use the ``invoke``
method of the ``task`` object and pass the memory-mapped interface as
values:

.. code-block:: cpp

  void NestedTask(tapa::mmap<const int> mem) {
    // Task logic
  }

  void Task(tapa::mmap<const int> mem) {
    tapa::task().invoke(NestedTask, mem);
  }

.. note::

   TAPA requires the direction of data flow to be specified in the top-level
   task invocation. Memory-mapped interfaces can be passed to nested tasks
   as values.

Memory Access
^^^^^^^^^^^^^

Memory-mapped interfaces can be accessed using the array subscript operator
``[]`` as if they were arrays:

.. code-block:: cpp

  void Task(tapa::mmap<const int> mem) {
    int data = mem[0];
  }

.. note::

   Memory-mapped interfaces can be accessed as if they were arrays.

Stream and MMAP Arrays
----------------------

TAPA supports arrays of streams (``istreams``/``ostreams``) and memory-mapped
interfaces (``mmaps``) to facilitate parameterized designs and reduce code
repetition (:ref:`tapa::streams <api streams>`/:ref:`tapa::mmaps <api mmaps>`).
This feature is particularly useful for creating flexible, scalable designs.

.. tip::

   A singleton ``stream`` or ``mmap`` is insufficient for parameterized
   designs. For example, the
   `network app <https://github.com/tuna/tapa/blob/main/tests/apps/network/network.cpp>`_
   shipped with TAPA defines an 8×8 switch network. What if we want to use a
   16×16 network? Or 4×4? TAPA allows parameterization of network size
   through arrays of ``stream``/``mmap`` and batch invocation.

With TAPA, you can define arrays of streams and memory-mapped interfaces and
invoking multiple tasks in parallel using ``invoke<..., n>``, where ``n``
is the number of invocations:

1. For each task instantiation, the ``streams`` or ``mmaps`` array arguments
   are accessed in sequence from the array, distributing the elements across
   multiple invocations.
2. If the formal parameter is a singleton (``istream``, ``ostream``, ``mmap``,
   ``async_mmap``), only one element in the array is accessed.
3. If the formal parameter is an array (``istreams``, ``ostreams``, ``mmaps``),
   the number of elements accessed is determined by the array length of the
   formal parameter.

Example usage from the
`TAPA network app <https://github.com/tuna/tapa/blob/main/tests/apps/network/network.cpp>`_:

.. code-block:: cpp

  void Switch2x2(int b, istream<pkt_t>& pkt_in_q0, istream<pkt_t>& pkt_in_q1,
                 ostreams<pkt_t, 2>& pkt_out_q) {
  }

  void InnerStage(int b, istreams<pkt_t, kN / 2>& in_q0,
                  istreams<pkt_t, kN / 2>& in_q1, ostreams<pkt_t, kN> out_q) {
    task().invoke<detach, kN / 2>(Switch2x2, b, in_q0, in_q1, out_q);
  }

In the ``InnerStage`` function:

1. It instantiates the ``Switch2x2`` task ``kN / 2`` times using a single
   ``invoke<..., kN / 2>``.
2. The first argument ``b`` is a scalar input, broadcast to each ``Switch2x2``
   instance.
3. The second argument ``in_q0`` is an ``istreams<pkt_t, kN / 2>`` array. Each
   of the ``Switch2x2`` instances takes one ``istream<pkt_t>``, as
   the formal parameter is a singleton (``istream``).
4. The third argument ``in_q1`` is accessed similarly to ``in_q0``.
5. The fourth argument ``out_q`` is an ``ostreams<pkt_t, kN>`` array. Each
   ``Switch2x2`` instance takes one ``ostreams<pkt_t, 2>``, which is
   effectively two ``ostream<pkt_t>``.

.. warning::

   TAPA does not support accessing individual streams or memory-mapped
   interfaces from a ``streams`` or ``mmaps`` array, unless it is in the leaf
   level of the task invocation hierarchy. For example, ``in_q0[0]`` will not
   compile. Instead, use ``tapa::invoke<..., n>`` to distribute the array
   elements across multiple invocations.

.. note::

   TAPA uses ``istreams``, ``ostreams``, and ``mmaps`` to support arrays of
   streams and memory-mapped interfaces, and they are distributed across
   multiple invocations using ``invoke<..., n>``.

Asynchronous Memory Access
--------------------------

TAPA's ``async_mmap`` provides a flexible interface for external memory access
through the AXI protocol. It exposes the five AXI channels (``AR``, ``R``,
``AW``, ``W``, ``B``) in C++, giving users maximal control over memory access
patterns.

.. tip::

   ``async_mmap`` provides richer memory access patterns expressiveness than
   the traditional ``mmap`` interface with much smaller area overhead.

.. note::

   ``async_mmap`` supports runtime burst detection to optimize memory access.

Structure and Channels
^^^^^^^^^^^^^^^^^^^^^^

The ``async_mmap`` is defined as follows:

.. code-block:: cpp

  template <typename T>
  struct async_mmap {
    using addr_t = int64_t;
    using resp_t = uint8_t;

    tapa::ostream<addr_t> read_addr;
    tapa::istream<T> read_data;
    tapa::ostream<addr_t> write_addr;
    tapa::ostream<T> write_data;
    tapa::istream<resp_t> write_resp;
  };

This structure abstracts an external memory as an interface consisting of five
streams:

1. ``read_addr``: Output stream for read addresses.
2. ``read_data``: Input stream for read data.
3. ``write_addr``: Output stream for write addresses.
4. ``write_data``: Output stream for write data.
5. ``write_resp``: Input stream for write responses.

The ``async_mmap`` structure is illustrated in the following diagram:

.. image:: https://user-images.githubusercontent.com/32432619/162324279-93f2dd34-73a6-4fa5-a4df-afd032b94b80.png
  :width: 100 %

Usage Model
^^^^^^^^^^^

- Read operations:

  - Send an address to the ``read_addr`` channel.
  - Receive the corresponding data of type ``T`` from the ``read_data``
    channel.
  - Multiple read requests can be issued before receiving responses

- Write operations:

  - Send an address to the ``write_addr`` channel.
  - Send the corresponding data to the ``write_data`` channel.
  - The ``write_resp`` channel will receive data indicating how many write
    transactions have succeeded.

Basic Usage
^^^^^^^^^^^

``async_mmap`` should be used only as formal parameters in leaf-level tasks.
It can be constructed from ``mmap``, and an ``mmap`` argument can be passed to
an ``async_mmap`` parameter. Note that the only the non-blocking API ``try_ready``
and ``try_write`` should be used to avoid deadlocks. Detailed usage can be found at
:ref:`Efficient Memory Accesses <tutorial/async_mmap:Efficient Memory Accesses>`

.. warning::

   ``async_mmap`` should only be used as formal parameters in leaf-level tasks,
   which are C++ functions that are called directly from ``tapa::task::invoke``
   and do not instantiate any children tasks or streams

.. warning::
   Due to certain from the Vitis HLS compiler, ``async_mmap`` must be passed
   by reference, i.e., with ``&``. In contrast, ``mmap`` must be passed by
   value, i.e., without ``&``.

.. code-block:: cpp

  void task1(tapa::async_mmap<data_t>& mem);
  void task2(tapa::      mmap<data_t>  mem);

  // Note the &
  void task1(tapa::async_mmap<data_t>& mem) {
    // ...
    mem.read_addr.try_write(...);
    // ...
    mem.read_data.try_read(...);
    // ...
  }

  // Note no &
  void task2(tapa::mmap<data_t> mem) {
    // ...
    mem[i] = foo;
    bar = mem[j];
    // ...
  }

  void top(tapa::mmap<data_t> mem1, tapa::mmap<data_t> mem2) {
    tapa::task()
      .invoke(task1, mem1)
      .invoke(task2, mem2)
      ;
  }

Runtime Burst Detection
^^^^^^^^^^^^^^^^^^^^^^^

TAPA infers burst ``async_mmap`` transactions at runtime, allowing for
efficient memory access in both sequential and random access patterns.
Users only need to issue individual read/write transactions, and TAPA
optimizes them into burst transactions when possible.

This approach offers several advantages:

1. More efficient for both sequential and random access patterns.
2. No reliance on static analysis for burst inference.
3. Allows for dynamic, data-dependent access patterns.

.. raw:: html

   <details>
   <summary><a>What are bursts?</a></summary>
   <br/>


``mmap`` (which uses Vitis HLS ``#pragma HLS interface m_axi`` internally)
provides synchronous memory interfaces that heavily rely on memory bursts.
Without memory bursts, the access pattern looks like this:

.. figure:: ../figures/tapa-sync-mmap-no-burst.drawio.svg
  :width: 100 %

  Synchronous off-chip memory accesses without burst.

A significant issue is that long memory latency
(`typically 100 ~ 200 ns <https://arxiv.org/abs/2010.06075>`_)
can result in very low memory throughput. To address this, memory bursts
are widely used, allowing the kernel to receive multiple data pieces using
a single memory request:

.. figure:: ../figures/tapa-sync-mmap-burst.drawio.svg
  :width: 100 %

  Synchronous off-chip memory accesses with burst.

However, memory bursts are only available for consecutive memory access
patterns. To overcome this limitation, TAPA ``async_mmap`` uses a different
approach, issuing multiple outstanding requests simultaneously:

.. figure:: ../figures/tapa-async-mmap.drawio.svg
  :width: 100 %

  Asynchronous off-chip memory accesses.

Multi-outstanding asynchronous requests are much more efficient than
single-outstanding synchronous requests. However, for sequential access
patterns, large burst memory accesses are still significantly more efficient
than small individual transactions on external memory. For instance, reading
4 KB of data in one AXI transaction is much faster than 512 smaller 8-byte
AXI transactions. Current HLS tools (e.g., Vitis HLS) typically use static
analysis to infer bursts, which may result in unpredictable and limited
hardware.

TAPA, instead, infers burst transactions at runtime. Users only need to issue
individual read/write transactions, and TAPA provides optimized modules to
combine and merge sequential transactions into burst transactions dynamically.

.. figure:: ../figures/tapa-async-mmap-burst.drawio.svg
  :width: 100 %

  Asynchronous off-chip memory accesses with runtime burst detection.

.. raw:: html

   </details>
   <br/>

Smaller Area Overhead
^^^^^^^^^^^^^^^^^^^^^

Compared to Vitis HLS, TAPA's ``async_mmap`` implementation results in
significantly smaller area overhead. This is particularly beneficial
for HBM devices with multiple memory channels.

Quantitative results from
`a microbenchmark <https://escholarship.org/uc/item/404825zp>`_:

=============================== =========  ==== ==== ==== ==== ===
Memory Interface                Clock/MHz  LUT  FF   BRAM URAM DSP
=============================== =========  ==== ==== ==== ==== ===
``#pragma HLS interface m_axi``       300  1189 3740   15    0   0
``async_mmap``                        300  1466  162    0    0   0
=============================== =========  ==== ==== ==== ==== ===

As shown, ``async_mmap`` uses significantly fewer BRAM resources and
flip-flops, making it more efficient for designs with multiple memory
interfaces.

.. note::

   By using asynchronous memory interfaces and runtime burst detection,
   ``async_mmap`` enables high memory throughput for both sequential and
   random memory accesses with minimal area overhead.

Sharing Memory Interfaces
-------------------------

TAPA provides the flexibility to share memory-mapped interfaces among
dataflow modules, a feature not available in Vitis HLS. This capability
is particularly useful when the number of memory-mapped interfaces is limited.

Example: Shared Vector Add
^^^^^^^^^^^^^^^^^^^^^^^^^^

The shared vector add example shipped with TAPA demonstrates this capability
by putting the inputs ``a`` and ``b`` in the same memory-mapped interface.

.. code-block:: cpp

  void Mmap2Stream(mmap<float> mmap, int offset, uint64_t n, ostream<float>& stream) {
    for (uint64_t i = 0; i < n; ++i) {
      stream.write(mmap[n * offset + i]);
    }
    stream.close();
  }

  void Load(mmap<float> srcs, uint64_t n, ostream<float>& a, ostream<float>& b) {
    task()
        .invoke(Mmap2Stream, srcs, 0, n, a)
        .invoke(Mmap2Stream, srcs, 1, n, b);
  }

- The same ``mmap<float>`` is referenced twice in the ``Load`` function.
- Two ``Mmap2Stream`` task instances can access the same AXI instance.
- The ``offset`` parameter in ``Mmap2Stream`` allows for accessing different
  parts of the shared memory.

Implementation Details
^^^^^^^^^^^^^^^^^^^^^^

Under the hood, TAPA implements this sharing mechanism as follows:

1. **AXI Interconnect**: TAPA instantiates an AXI interconnect to manage
   access to the shared memory interface.
2. **Dedicated AXI Threads**: Each port using the shared interface gets a
   dedicated AXI thread.
3. **Unordered Requests**: Requests from different ports are not ordered
   with respect to each other. This helps reduce potential deadlocks.

.. warning::

   **Memory Consistency**: The programmer needs to ensure memory consistency
   among shared memory-mapped interfaces. This typically involves accessing
   different memory locations in different task instances.

.. note::

   TAPA allows sharing memory-mapped interfaces among dataflow modules,
   reducing the number of memory interfaces required.
