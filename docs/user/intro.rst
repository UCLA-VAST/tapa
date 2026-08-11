Introduction
============

What is TAPA?
-------------

TAPA (\ **Ta**\ sk-\ **Pa**\ rallel) is an end-to-end framework
designed for creating high-frequency FPGA dataflow accelerators. It combines
a powerful C++ API with advanced physical-design optimization techniques
to deliver high design performance and productivity.

TAPA enables developers to express complex, task-parallel FPGA
designs using familiar and standard ``g++``-compilable C++ syntax while
leveraging FPGA-specific optimizations, aiming to bridge the gap between
high-level design description and efficient hardware implementation.

The open-source `TAPA Compiler`_ provides a powerful C++ API for expressing
task-parallel accelerators and compiles the design into Verilog RTL.

.. _TAPA Compiler: https://github.com/tuna/tapa

TAPA Programming Model
----------------------

TAPA compiler introduces an HLS programming model by focusing on task-parallel
dataflow designs. In this model, parallel HLS ``tasks`` communicate with each
other through ``streams``.

- **Tasks** are independent units of computation that execute concurrently,
  defined as C++ functions invoked by the TAPA runtime.
- **Streams** are FIFO-like communication channels connecting tasks, instantiated
  as C++ objects in the TAPA tasks.
- Tasks read from streams, perform computation as defined in the task
  function, and write to other streams.

The TAPA compiler synthesizes these high-level task-parallel descriptions into
standalone, fully-functional Verilog RTL, which can be co-simulated with the
original C++ code using the TAPA runtime, or synthesized into FPGA bitstreams
for deployment with the same TAPA runtime.

.. image:: https://user-images.githubusercontent.com/32432619/167315788-4f4c7241-d7bb-454d-80d2-94a3eae505a5.png
  :width: 100 %

TAPA offers several advantages over other FPGA accelerations solutions like
Intel FPGA SDK for OpenCL and Xilinx Vitis.

Unlike Intel's approach, which limits kernel instances and communication
channels to global variables, TAPA allows for hierarchical designs and
easier code sharing among kernels. TAPA also provides clearer visibility
of accessed channels for each kernel and enables efficient synthesis of
functionally identical kernels.

TAPA overcomes Xilinx Vitis's limitations by supporting both fine-grained
and coarse-grained task parallelism within a unified framework, eliminating
the need for separate OpenCL kernels and complex linking processes. TAPA's
stream interfaces are more consistent and generalizable across different
granularities of parallelism.

With the TAPA programming model, developers can:

- **Express** complex dataflow designs in C++ with a high-level API.
- **Scale** the design by composing tasks and streams.
- **Debug** the design using standard C++ debugging tools and techniques.
- **Accelerate** development with the TAPA compiler's fast compilation time.
- **Optimize** the design for high frequency and resource utilization using
  floorplanning and pipelining.

.. note::

   These features collectively contribute to TAPA providing a more flexible,
   productive, and higher-quality development experience for FPGA programming
   compared to other solutions.

Physical-Design Optimization
----------------------------

TAPA automatically optimizes designs for high
performance through partition-and-pipeline optimization:

- **Intelligent Partitioning**: TAPA automatically floorplans the design
  across the FPGA, achieving balanced resource utilization and reducing local
  congestion.
- **Pipeline Insertion**: TAPA inserts pipeline registers between tasks
  to maximize the design frequency and mitigate long-wire delays.

This dual-pronged approach yields two critical benefits:

1. **Reduced Local Congestion**: By spreading logic across the entire FPGA,
   TAPA avoids over-utilization hotspots and minimizes local congestion.
2. **Optimized Global Paths**: By efficiently pipelining long-distance
   connections, TAPA minimizes wire delays and maintains high clock
   frequency.

Summary
-------

TAPA is a powerful framework for designing high-frequency FPGA
dataflow accelerators. It provides the following key advantages:

- **Rapid Development**: TAPA compiler accelerates development with fast
  compilation times and familiar C++ syntax. It enables 7× faster compilation
  and 3× faster software simulation than conventional approaches.
- **Expressive API**: TAPA compiler provides rich and modern C++ syntax with
  dedicated constructs for complex memory access patterns and explicit
  parallelism.
- **Scalability**: TAPA compiler scales designs by encapsulating complex
  dataflow patterns into reusable tasks and streams.

The physical-design optimization further enhances the design performance:

- **High-Frequency Performance**: TAPA optimizes designs for high
  frequency, achieving 2× higher frequency on average compared to traditional
  HLS tools.
- **HBM Optimizations**: TAPA optimizes designs for HBM-based
  FPGAs, automating design space exploration and physical optimizations.

Whether you're working on complex algorithms, data processing pipelines,
or custom accelerators, TAPA provides the tools and optimizations
needed to maximize your FPGA's potential.
