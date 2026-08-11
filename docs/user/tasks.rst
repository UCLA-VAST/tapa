Tasks and Hierarchical Design
=============================

.. note::

   TAPA allows flexible task invocation with features such as detached
   tasks and hierarchical design. This section provides an overview of
   TAPA tasks and their advanced features.

TAPA Tasks
----------

TAPA tasks are the fundamental building blocks of a TAPA design. A task is a
C++ function that can be invoked by the TAPA runtime to perform computation
on an FPGA. Tasks communicate with each other through streams, and the
external memory interface is abstracted as a memory map (``mmap``).

Here's an example of a TAPA task:

.. code-block:: cpp

  void Task(int a,
            tapa::istream<int> &in, tapa::ostream<int> &out,
            tapa::mmap<int> mem) {
    // ... task logic ...
  }

.. note::

   TAPA does not support template functions as tasks. To use a template
   as a task, you must wrap it in a non-template function.

TAPA tasks usually has scalar arguments, input streams, output streams, and
memory maps.

To invoke a task, use the ``task().invoke`` method:

.. code-block:: cpp

   tapa::task().invoke(Task, 42, in, out, mem);

In this example, we invoke the ``Task`` function with the scalar argument
``42``, input stream ``in``, output stream ``out``, and memory map ``mem``.

.. note::

   TAPA tasks are C++ functions that can be invoked by the TAPA runtime to
   perform computation on an FPGA. Tasks communicate with each other through
   streams, and the external memory interface is abstracted as a memory map.

Leaf Tasks
----------

A leaf task is a task that doesn't invoke other tasks. Leaf tasks may perform
a specific computation. They are the building blocks of a TAPA design and
are invoked by the upper-level tasks.

A leaf module could contain arbitrary computation in HLS C++. For example,
the following leaf task ``Task`` reads from an input stream, performs a
computation, and writes to an output stream:

.. code-block:: cpp

   void Task(int a, tapa::istream<int> &in, tapa::ostream<int> &out) {
     int x = in.read();
     out.write(x + a);
   }

.. warning::

   Leaf tasks should not call other TAPA tasks. However, they can call
   other C++ functions.

Upper-Level Tasks
-----------------

An upper-level task is a task that invokes other tasks. Upper-level tasks
orchestrate the execution of leaf tasks and manage the dataflow between them.
They are responsible for instantiating streams, passing them to child tasks,
and invoking them.

Note that upper-level tasks could only contain the instantiation of streams
and the invocation of other tasks. They should not contain any computation
logic. It must follow this structure:

.. code-block:: cpp

   void UpperLevel(/* arguments */) {
     tapa::stream<int> in("in");
     /* and other streams */
     tapa::task().invoke(Task, /* arguments */)
                 .invoke(/* other tasks */);
   }

Top-Level Task
--------------

The top-level task in a TAPA design is the entry point for the FPGA
accelerator. It orchestrates the execution of other tasks and manages the
dataflow between them. The top-level task is responsible for instantiating
streams, passing them to child tasks, and invoking them.

A top-level task must be in the form of an upper-level task. For example,
the top-level task ``TopLevel`` invokes the task ``Task``:

.. code-block:: cpp

   void TopLevel(tapa::mmap<int> mem) {
     tapa::stream<int> in("in");
     tapa::stream<int> out("out");
     tapa::task().invoke(Task, 42, in, out, mem);
   }

The top-level task ``TopLevel`` should be invoked from the host program to
start the FPGA accelerator:

.. code-block:: cpp

   tapa::invoke(TopLevel, bitstream_path, mem);

.. note::

   The top-level task in a TAPA design is the entry point for the FPGA
   accelerator, which should be invoked from the host program.

Detached Tasks
--------------

TAPA allows you to *detach* a task on invocation instead of joining it to
the parent. This feature is useful when terminating each kernel function
is unnecessary, such as for purely data-driven tasks that don't need to be
terminated on program completion. Detached tasks in TAPA are similar to
``std::thread::detach`` in the
`C++ STL <https://en.cppreference.com/w/cpp/thread/thread/detach>`_.

To detach a task, use the ``tapa::detach`` keyword in the task invocation:

.. code-block:: cpp

  void InnerStage(int b, istreams<pkt_t, kN / 2>& in_q0,
                  istreams<pkt_t, kN / 2>& in_q1, ostreams<pkt_t, kN> out_q) {
    task().invoke<tapa::detach, kN / 2>(Switch2x2, b, in_q0, in_q1, out_q);
  }

Detached tasks offer several advantages:

1. **Design efficiency**: The state of detached tasks doesn't need to be
   maintained at runtime, saving resources. Fan-out signals are also
   avoided, reducing the complexity of the design.
2. **Programming flexibility**: Unlike Intel FPGA SDK's
   `"autorun" kernels <https://www.intel.com/content/www/us/en/programmable/documentation/mwh1391807965224.html#ewa1456413600674>`_
   or Xilinx Vitis'
   `"free-running" kernels <https://www.xilinx.com/html_docs/xilinx2020_2/vitis_doc/streamingconnections.html#ariaid-title5>`_,
   detached tasks in TAPA can have arguments other than global communication
   channels.
3. **Simplified design**: Detached tasks eliminate the need to propagate
   termination signals, reducing unnecessary complexity.

.. note::

   By default, TAPA tasks are joined on invocation. Therefore, the parent
   task waits for the child task to complete before terminating. To detach
   the task, use the ``tapa::detach`` keyword in the task invocation.

Hierarchical Design
-------------------

TAPA supports hierarchical task design, allowing tasks to be recursively
defined. A task can be a parent to its children tasks, which can themselves
be parents to their own children. This feature enables the creation of
complex, modular designs.

Here's an example of a hierarchical design using TAPA:

.. code-block:: cpp

  void Stage(int b, istreams<pkt_t, kN>& in_q, ostreams<pkt_t, kN> out_q) {
    task().invoke<detach>(InnerStage, b, in_q, in_q, out_q);
  }

  void Network(mmap<vec_t<pkt_t, kN>> mmap_in, mmap<vec_t<pkt_t, kN>> mmap_out,
               uint64_t n) {
    streams<pkt_t, kN, 4096> q0("q0");
    streams<pkt_t, kN, 4096> q1("q1");
    streams<pkt_t, kN, 4096> q2("q2");
    streams<pkt_t, kN, 4096> q3("q3");

    task()
        .invoke(Produce, mmap_in, n, q0)
        .invoke(Stage, 2, q0, q1)
        .invoke(Stage, 1, q1, q2)
        .invoke(Stage, 0, q2, q3)
        .invoke(Consume, mmap_out, n, q3);
  }

In this example, the top-level task instantiates a data producer, a data
consumer, and three wrapper stages. Each stage can further instantiate its
own child tasks ``InnerStage``, creating a hierarchical structure.

The
`network example <https://github.com/tuna/tapa/blob/main/tests/apps/network/network.cpp>`_
shipped with TAPA demonstrates both of these features:

1. It uses detached tasks for 2×2 switch boxes, which are instantiated and
   detached in the inner wrapper stage.
2. It employs a hierarchical design where 2×2 switch boxes are instantiated
   in an inner wrapper stage, and each inner stage is then wrapped in a stage.

.. note::

   TAPA allows tasks to be detached on invocation and supports hierarchical
   design, enabling the creation of complex, modular designs.
