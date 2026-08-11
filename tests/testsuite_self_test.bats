# Self test for the testsuite if the environment is set correctly
#
# Justification for using bats instead of Bazel for the testsuite:
# Bats mimics the behavior of a user running the tests manually when
# installed on the system, and better reflects the user experience.

# Copyright (c) 2024 RapidStream Design Automation, Inc. and contributors.
# All rights reserved. The contributor(s) of this file has/have agreed to the
# RapidStream Contributor License Agreement.

@test "testsuite: TAPA_HOME is set" {
  [ -d "${TAPA_HOME}" ]
}

@test "testsuite: TAPA_HOME/usr/include exists" {
  [ -d "${TAPA_HOME}/usr/include" ]
}

@test "testsuite: TAPA_HOME/usr/lib exists" {
  [ -d "${TAPA_HOME}/usr/lib" ]
}

@test "testsuite: tapa is runnable" {
  tapa --help
}

@test "testsuite: tapa-fast-cosim is runnable" {
  tapa-fast-cosim --help
}

@test "testsuite: XILINX_HLS is set" {
  [ -d "${XILINX_HLS}" ]
}

@test "testsuite: vitis_hls is runnable" {
  vitis_hls --version
}
