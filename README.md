# TAPA (Publication Version)

> [!IMPORTANT]
> **This repository is archived and no longer maintained.**
>
> TAPA has moved to **[github.com/tuna/tapa](https://github.com/tuna/tapa)**,
> which is the **only actively maintained** repository. Please use it for the
> latest code, releases, documentation, issues, and pull requests.

## Installing TAPA

Install the latest release from the new repository:

```bash
curl -fsSL https://raw.githubusercontent.com/tuna/tapa/main/install.sh | sh -s -- -q
```

With root privileges, this installs to `/opt/tapa` (symlinks in
`/usr/local/bin`). Without root, it installs to `~/.tapa` and updates your
shell `PATH`.

To install a specific version:

```bash
curl -fsSL https://raw.githubusercontent.com/tuna/tapa/main/install.sh \
  | TAPA_VERSION=<version> sh -s -- -q
```

See all releases at [github.com/tuna/tapa/releases](https://github.com/tuna/tapa/releases).

**Requirements:** Linux (Ubuntu 18.04+, Debian 10+, RHEL 9+, Fedora 34+, Amazon
Linux 2023), `g++` 7.5.0+, and the [CBC](https://github.com/coin-or/Cbc) ILP
solver (`coinor-cbc` on Ubuntu/Debian, `coin-or-Cbc` on Fedora/EPEL) for
floorplanning. Vitis HLS 2022.1+ is required for RTL synthesis and on-board
execution — **not** for software simulation.

To build from source instead, clone the new repository and follow its
instructions:

```bash
git clone https://github.com/tuna/tapa.git
cd tapa
```

## About TAPA

TAPA (**Ta**sk-**Pa**rallel) is a powerful framework for designing
high-frequency FPGA dataflow accelerators. It combines a **powerful C++ API**
for expressing task-parallel designs with **advanced optimization techniques**
to deliver exceptional design performance and productivity.

- **High-Frequency Performance**: Achieve 2× higher frequency on average
  compared to Vivado[<sup>1</sup>](https://doi.org/10.1145/3431920.3439289).
- **Rapid Development**: 7× faster compilation and 3× faster software
  simulation than Vitis HLS[<sup>2</sup>](https://doi.org/10.1109/fccm51124.2021.00032).
- **Expressive API**: Rich C++ syntax with dedicated APIs for complex memory
  access patterns and explicit parallelism.
- **HBM Optimizations**: Automated design space exploration and physical
  optimizations for HBM FPGAs.

This repository contains the publication version of TAPA accompanying the
papers listed [below](#publications). It is kept for archival and reproducibility
purposes only.

## Documentation

Documentation is maintained with the new repository:

- [User Guide](https://tapa.readthedocs.io/en/main/).
- [Quick Reference](https://tapa.readthedocs.io/en/main/user/cheatsheet.html).
- [Installation Guide](https://tapa.readthedocs.io/en/main/user/installation.html).
- [Getting Started](https://tapa.readthedocs.io/en/main/user/getting_started.html).
- [API Reference](https://tapa.readthedocs.io/en/main/api.html).

## Success Stories

- [Serpens](https://dl.acm.org/doi/10.1145/3489517.3530420) (DAC'22): 270 MHz
  on Xilinx Alveo U280 HBM board with 24 HBM channels, while the Vitis HLS
  baseline failed in routing.
- [Sextans](https://dl.acm.org/doi/pdf/10.1145/3490422.3502357) (FPGA'22):
  260 MHz on Xilinx Alveo U250 board with 4 DDR channels, while the Vivado
  baseline achieves only 189 MHz.
- [SPLAG](https://github.com/UCLA-VAST/splag) (FPGA'22): Up to 4.9× speedup
  over state-of-the-art FPGA accelerators, up to 2.6× speedup over 32-thread
  CPU running at 4.4 GHz, and up to 0.9× speedup over an A100 GPU.
- [AutoSA Systolic-Array Compiler](https://github.com/UCLA-VAST/AutoSA)
  (FPGA'21): Significant frequency improvements over the Vitis HLS baseline.
- [KNN](https://github.com/SFU-HiAccel/CHIP-KNN) (FPT'20): 252 MHz on Xilinx
  Alveo U280 board, compared to 165 MHz with the Vivado baseline.

## Publications

1. Licheng Guo, Yuze Chi, Jie Wang, Jason Lau, Weikang Qiao, Ecenur Ustun, Zhiru Zhang, Jason Cong.
   [AutoBridge: Coupling coarse-grained floorplanning and pipelining for high-frequency HLS design on multi-die FPGAs](https://doi.org/10.1145/3431920.3439289).
   FPGA, 2021. (Best Paper Award)
2. Yuze Chi, Licheng Guo, Jason Lau, Young-kyu Choi, Jie Wang, Jason Cong.
   [Extending high-level synthesis for task-Parallel programs](https://doi.org/10.1109/fccm51124.2021.00032).
   FCCM, 2021.
3. Young-kyu Choi, Yuze Chi, Jason Lau, Jason Cong.
   [TARO: Automatic optimization for free-running kernels in FPGA high-level synthesis](https://doi.org/10.1109/TCAD.2022.3216544).
   TCAD, 2022.
4. Licheng Guo, Pongstorn Maidee, Yun Zhou, Chris Lavin, Eddie Hung, Wuxi Li, Jason Lau, Weikang Qiao, Yuze Chi, Linghao Song, Yuanlong Xiao, Alireza Kaviani, Zhiru Zhang, Jason Cong.
   [RapidStream 2.0: Automated parallel implementation of latency insensitive FPGA designs through partial reconfiguration](https://doi.org/10.1145/3593025).
   TRETS, 2023.
5. Licheng Guo, Yuze Chi, Jason Lau, Linghao Song, Xingyu Tian, Moazin Khatti, Weikang Qiao, Jie Wang, Ecenur Ustun, Zhenman Fang, Zhiru Zhang, Jason Cong.
   [TAPA: A scalable task-parallel dataflow programming framework for modern FPGAs with co-optimization of HLS and physical design](https://doi.org/10.1145/3609335).
   TRETS, 2023.

## Licensing

TAPA is open-source software licensed under the MIT license. For full license
details, please refer to the [LICENSE](LICENSE) file.

---

Copyright (c) 2024 RapidStream Design Automation, Inc. and contributors.<br/>
Copyright (c) 2020 Yuze Chi and contributors.<br/>
