# Copyright (c) 2024 RapidStream Design Automation, Inc. and contributors.
# All rights reserved. The contributor(s) of this file has/have agreed to the
# RapidStream Contributor License Agreement.

# TAPA running script template

# This script demonstrates how to csynth TAPA applications to XO files and
# cosimulate the design with TAPA.

APP=VecAdd
CSYNTH_FILE=vadd.cpp
ARGS="1000"

# Synthesizing the app to generate the XO file
tapa \
  compile \
  --top $APP \
  --part-num xcu250-figd2104-2L-e \
  --clock-period 3.33 \
  -f $CSYNTH_FILE \
  -o work.out/$APP.xo

# repack with custom rtl files
tapa \
  pack \
  -o work.out/$APP.xo \
  --custom-rtl ./rtl/

# Compile the host with TAPA
tapa \
  g++ \
  ./*.cpp \
  -o $APP

# Cosim the design with TAPA
./$APP --bitstream build/$APP.xo $ARGS
