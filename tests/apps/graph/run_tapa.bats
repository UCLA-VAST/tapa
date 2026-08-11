# Copyright (c) 2024 RapidStream Design Automation, Inc. and contributors.
# All rights reserved. The contributor(s) of this file has/have agreed to the
# RapidStream Contributor License Agreement.

compile_host() {
  cd "${BATS_TEST_DIRNAME}"

  tapa g++ \
    graph-host.cpp graph.cpp \
    -o ${BATS_TMPDIR}/graph-host

  [ -x "${BATS_TMPDIR}/graph-host" ]
}

compile_xo() {
  cd "${BATS_TEST_DIRNAME}"

  ${TAPA_HOME}/usr/bin/tapa \
    -w ${BATS_TMPDIR}/graph-workdir \
    compile \
    --jobs 2 \
    --platform xilinx_u250_gen3x16_xdma_4_1_202210_1 \
    -f graph.cpp \
    -t Graph \
    -o ${BATS_TMPDIR}/graph.xo

  [ -f "${BATS_TMPDIR}/graph.xo" ]
}

@test "apps/graph: tapa c simulation passes" {
  compile_host
  ${BATS_TMPDIR}/graph-host graph.txt
}

@test "apps/graph: tapa generates an xo file and its simulation passes" {
  compile_xo
  ${BATS_TMPDIR}/graph-host --bitstream ${BATS_TMPDIR}/graph.xo graph.txt
}
