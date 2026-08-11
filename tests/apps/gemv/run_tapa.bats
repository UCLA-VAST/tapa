# Copyright (c) 2024 RapidStream Design Automation, Inc. and contributors.
# All rights reserved. The contributor(s) of this file has/have agreed to the
# RapidStream Contributor License Agreement.

compile_host() {
  cd "${BATS_TEST_DIRNAME}"

  tapa g++ \
    gemv-host.cpp gemv.cpp \
    -o ${BATS_TMPDIR}/gemv-host

  [ -x "${BATS_TMPDIR}/gemv-host" ]
}

compile_xo() {
  cd "${BATS_TEST_DIRNAME}"

  ${TAPA_HOME}/usr/bin/tapa \
    -w ${BATS_TMPDIR}/gemv-workdir \
    compile \
    --jobs 2 \
    --platform xilinx_u250_gen3x16_xdma_4_1_202210_1 \
    -f gemv.cpp \
    -t Gemv \
    -o ${BATS_TMPDIR}/gemv.xo

  [ -f "${BATS_TMPDIR}/gemv.xo" ]
}

@test "apps/gemv: tapa c simulation passes" {
  compile_host
  ${BATS_TMPDIR}/gemv-host
}

@test "apps/gemv: tapa generates an xo file and its simulation passes" {
  compile_xo
  ${BATS_TMPDIR}/gemv-host --bitstream ${BATS_TMPDIR}/gemv.xo
}
