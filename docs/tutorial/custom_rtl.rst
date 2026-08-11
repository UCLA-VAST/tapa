Design with Custom RTL Kernels
==============================

.. note::

   In this tutorial, we explore how to integrate custom RTL kernels into a TAPA design.

Example: Vector Add with Custom RTL Kernel
------------------------------------------

In this example, we will use a custom-designed RTL kernel to replace a non-synthesizable kernel in the example design.

The complete example can be found at `Custom RTL Test <https://github.com/tuna/tapa/tree/main/tests/functional/custom-rtl>`_.

Labeling the Non-synthesizable Kernel
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Given a TAPA design with the following tasks:

.. code-block:: cpp

    void Add(tapa::istream<float>& a, tapa::istream<float>& b,
             tapa::ostream<float>& c, uint64_t n) {
        int* c_dyn = new int[n];
        for (uint64_t i = 0; i < n; ++i) {
            c_dyn[i] = a.read() + b.read();
        }
        for (uint64_t i = 0; i < n; ++i) {
            c << c_dyn[i];
        }
        delete[] c_dyn;
    }

    [[tapa::target("non_synthesizable", "xilinx")]] void Add_Upper(
        tapa::istream<float>& a, tapa::istream<float>& b, tapa::ostream<float>& c,
        uint64_t n) {
        tapa::task().invoke(Add, a, b, c, n);
    }

The ``Add_Upper`` task contains a non-synthesizable child task, ``Add``, because it includes dynamic memory allocation, which is not supported by TAPA. In such cases, we can replace the non-synthesizable task with a custom RTL kernel. To replace the entire ``Add_Upper`` task with a custom RTL kernel, we label the task with the ``non_synthesizable`` attribute: ``[[tapa::target("non_synthesizable", "xilinx")]]``.

Compile with Non-synthesizable Task
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

After labeling the task, we can compile the design as usual, as shown in ``run.sh``:

.. code-block:: shell

    # Synthesizing the app to generate the XO file
    tapa \
        compile \
        --top $APP \
        --part-num xcu250-figd2104-2L-e \
        --clock-period 3.33 \
        -f $CSYNTH_FILE \
        -o work.out/$APP.xo

When compiling a design with non-synthesizable tasks, TAPA generates template files in the TAPA working directory. By default, these files are located at ``./work.out/template``. These template files provide a starting point for implementing the custom RTL kernel.

Repack the XO File
^^^^^^^^^^^^^^^^^^

Assuming the user has implemented the ``Add_Upper`` kernel with RTL files located in ``tests/functional/custom-rtl/rtl``, the user can repack the XO file with the custom RTL kernel by running the ``pack`` command:

.. code-block:: shell

    # Repack with custom RTL files
    tapa \
        pack \
        -o work.out/$APP.xo \
        --custom-rtl ./rtl/

The ``--custom-rtl`` flag specifies the file path of a custom RTL file or a directory containing custom RTL files. Multiple custom RTL paths can be specified by repeating the flag. The ``pack`` command performs format checking on the user-provided Verilog files and packs the XO file with the custom RTL files.
