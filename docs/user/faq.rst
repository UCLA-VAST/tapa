Frequently Asked Questions
==========================

Does TAPA support Alveo U55C?
-----------------------------

Yes, TAPA fully supports the Alveo U55C FPGA accelerator card.
For detailed information on how to use TAPA with the Alveo U55C,
please refer to our `Examples`_. You can find a specific example
in the `Sextans benchmark`_.

.. _Examples: https://github.com/tuna/tapa/tree/main/tests
.. _Sextans benchmark: https://github.com/tuna/tapa/tree/main/tests/regression/spmm/sextans-u55c-3x3floorplan

How can I incorporate floorplan information into TAPA?
------------------------------------------------------

TAPA supports floorplanning through slot-based device configurations and
floorplan constraint files. Please refer to the user guide of the
`TAPA repository`_ for comprehensive guidance and examples for effectively
utilizing floorplanning in your projects.

.. _TAPA repository: https://github.com/tuna/tapa

Where is ``tapac`` in TAPA?
---------------------------

The ``tapac`` command has been replaced by ``tapa compile``. The
``tapa compile`` command is used to compile TAPA source files into a Vitis
object file. Most of the arguments and options are the same as the original
``tapac`` command. You may refer to the ``--help`` option for more
information.

I encounter a "tapa command not found" error.
---------------------------------------------

If you encounter this error, it's likely that your system's ``PATH``
environment variable hasn't been updated to include the TAPA
installation directory. To resolve this:

1. Log out and log back in to your user account, or
2. Reboot your system.

.. note::

   Some systems may require you to reboot in order to update the ``PATH``.
   For instance, Visual Studio Code may not update the ``PATH`` until the
   last session is closed and timeouts, which may require a reboot.
   If a reboot is not feasible, you can manually update the ``PATH`` by
   following the instructions printed during the installation process.

Is there a command-line utility for quick help?
-----------------------------------------------

Yes, we provide a convenient command-line utility for quick reference. You
can access command information about our commands by typing:

.. code-block:: bash

   tapa --help
   tapa compile --help
