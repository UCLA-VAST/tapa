# Copyright (c) 2024 RapidStream Design Automation, Inc. and contributors.
# All rights reserved. The contributor(s) of this file has/have agreed to the
# RapidStream Contributor License Agreement.

compile_host() {
  cd "${BATS_TEST_DIRNAME}"

  tapa g++ \
    vadd-host.cpp vadd.cpp \
    -o ${BATS_TMPDIR}/vadd-host

  [ -x "${BATS_TMPDIR}/vadd-host" ]
}

compile_xo() {
  cd "${BATS_TEST_DIRNAME}"

  ${TAPA_HOME}/usr/bin/tapa \
    -w ${BATS_TMPDIR}/vadd-workdir \
    compile \
    --jobs 2 \
    --platform xilinx_u250_gen3x16_xdma_4_1_202210_1 \
    -f vadd.cpp \
    -t VecAdd \
    -o ${BATS_TMPDIR}/vadd.xo

  [ -f "${BATS_TMPDIR}/vadd.xo" ]
}

@test "apps/vadd: tapa c simulation passes" {
  compile_host
  ${BATS_TMPDIR}/vadd-host
}

@test "apps/vadd: tapa generates an xo file and its simulation passes" {
  compile_xo
  ${BATS_TMPDIR}/vadd-host --bitstream ${BATS_TMPDIR}/vadd.xo 1000
}
