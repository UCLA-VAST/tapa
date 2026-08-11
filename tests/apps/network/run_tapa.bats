# Copyright (c) 2024 RapidStream Design Automation, Inc. and contributors.
# All rights reserved. The contributor(s) of this file has/have agreed to the
# RapidStream Contributor License Agreement.

compile_host() {
  cd "${BATS_TEST_DIRNAME}"

  tapa g++ \
    network-host.cpp network.cpp \
    -o ${BATS_TMPDIR}/network-host

  [ -x "${BATS_TMPDIR}/network-host" ]
}

compile_xo() {
  cd "${BATS_TEST_DIRNAME}"

  ${TAPA_HOME}/usr/bin/tapa \
    -w ${BATS_TMPDIR}/network-workdir \
    compile \
    --jobs 2 \
    --platform xilinx_u250_gen3x16_xdma_4_1_202210_1 \
    -f network.cpp \
    -t Network \
    -o ${BATS_TMPDIR}/network.xo

  [ -f "${BATS_TMPDIR}/network.xo" ]
}

@test "apps/network: tapa c simulation passes" {
  compile_host
  ${BATS_TMPDIR}/network-host
}

@test "apps/network: tapa generates an xo file and its simulation passes" {
  compile_xo
  ${BATS_TMPDIR}/network-host --bitstream ${BATS_TMPDIR}/network.xo
}
