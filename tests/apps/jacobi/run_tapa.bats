# Copyright (c) 2024 RapidStream Design Automation, Inc. and contributors.
# All rights reserved. The contributor(s) of this file has/have agreed to the
# RapidStream Contributor License Agreement.

compile_host() {
  cd "${BATS_TEST_DIRNAME}"

  tapa g++ \
    jacobi-host.cpp jacobi.cpp \
    -o ${BATS_TMPDIR}/jacobi-host

  [ -x "${BATS_TMPDIR}/jacobi-host" ]
}

compile_xo() {
  cd "${BATS_TEST_DIRNAME}"

  ${TAPA_HOME}/usr/bin/tapa \
    -w ${BATS_TMPDIR}/jacobi-workdir \
    compile \
    --jobs 2 \
    --platform xilinx_u250_gen3x16_xdma_4_1_202210_1 \
    -f jacobi.cpp \
    -t Jacobi \
    -o ${BATS_TMPDIR}/jacobi.xo

  [ -f "${BATS_TMPDIR}/jacobi.xo" ]
}

@test "apps/jacobi: tapa c simulation passes" {
  compile_host
  ${BATS_TMPDIR}/jacobi-host
}

@test "apps/jacobi: tapa generates an xo file and its simulation passes" {
  compile_xo
  ${BATS_TMPDIR}/jacobi-host --bitstream ${BATS_TMPDIR}/jacobi.xo
}
