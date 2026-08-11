# Copyright (c) 2024 RapidStream Design Automation, Inc. and contributors.
# All rights reserved. The contributor(s) of this file has/have agreed to the
# RapidStream Contributor License Agreement.

compile_host() {
  cd "${BATS_TEST_DIRNAME}"

  tapa g++ \
    bandwidth-host.cpp bandwidth.cpp \
    -o ${BATS_TMPDIR}/bandwidth-host

  [ -x "${BATS_TMPDIR}/bandwidth-host" ]
}

compile_xo() {
  cd "${BATS_TEST_DIRNAME}"

  ${TAPA_HOME}/usr/bin/tapa \
    -w ${BATS_TMPDIR}/bandwidth-workdir \
    compile \
    --jobs 2 \
    --platform xilinx_u250_gen3x16_xdma_4_1_202210_1 \
    -f bandwidth.cpp \
    -t Bandwidth \
    -o ${BATS_TMPDIR}/bandwidth.xo

  [ -f "${BATS_TMPDIR}/bandwidth.xo" ]
}

@test "apps/bandwidth: tapa c simulation passes" {
  compile_host
  ${BATS_TMPDIR}/bandwidth-host
}

@test "apps/bandwidth: tapa generates an xo file and its simulation passes" {
  compile_xo
  ${BATS_TMPDIR}/bandwidth-host --bitstream ${BATS_TMPDIR}/bandwidth.xo 1000
}
