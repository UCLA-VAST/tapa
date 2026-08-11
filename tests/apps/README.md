<!--
Copyright (c) 2024 RapidStream Design Automation, Inc. and contributors.
All rights reserved. The contributor(s) of this file has/have agreed to the
RapidStream Contributor License Agreement.
-->

## General

This directory contains multiple small example TAPA designs:

| Example | Demonstrates |
|---|---|
| `vadd` | The core model: streams, `mmap`, and a four-task graph |
| `async_mmap` | Decoupled memory access with `tapa::async_mmap` |
| `bandwidth` | HBM bandwidth over many `async_mmap` channels |
| `cannon` | Cannon's matrix multiply on a 2D array of stream-connected PEs |
| `gemv` | Matrix-vector multiply |
| `graph` | Graph traversal with a large per-task local buffer |
| `ignore` | `[[tapa::target("ignore")]]` for custom-RTL replacement |
| `jacobi` | Stencil computation with end-of-transmission (`close()`) |
| `network` | Packet switching with `peek` and detached tasks |
| `templated` | Templated leaf tasks |

For large and complex designs, refer to the `tests/regression` directory.

To run the examples, build TAPA from source and follow the instructions below.
```bash
cd tapa/tests/apps/vadd
tapa g++ -- vadd.cpp vadd-host.cpp -o vadd
./vadd
```

Each example is also wired up as a Bazel target, so you can run one directly:

```bash
bazel test //tests/apps/vadd:vadd          # software simulation
bazel test //tests/apps/vadd:vadd-xosim    # RTL cosimulation of the .xo
```

The steps for building TAPA are at
<https://tapa.readthedocs.io/en/latest/developer/build.html>.
