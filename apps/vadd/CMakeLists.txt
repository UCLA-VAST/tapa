include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/apps.cmake)

add_executable(vadd)
target_sources(vadd PRIVATE vadd-host.cpp vadd.cpp)
target_link_libraries(vadd PRIVATE ${TAPA})
add_test(NAME vadd COMMAND vadd)

find_package(SDx)
if(SDx_FOUND)
  add_tapa_target(
    vadd-hw-xo
    INPUT vadd.cpp
    FRT_INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/vadd.frt.cpp
    TOP VecAdd
    PLATFORM xilinx_u250_xdma_201830_2)

  add_xocc_hw_link_targets(
    ${CMAKE_CURRENT_BINARY_DIR}
    INPUT vadd-hw-xo
    HW_EMU_XCLBIN
    hw_emu_xclbin
    HW_XCLBIN
    hw_xclbin)

  add_executable(vadd-frt)
  target_include_directories(vadd-frt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_sources(vadd-frt PRIVATE vadd-host.cpp
                                  ${CMAKE_CURRENT_BINARY_DIR}/vadd.frt.cpp)
  target_link_libraries(vadd-frt PRIVATE ${TAPA} frt::frt)

  add_custom_target(
    vadd-cosim
    COMMAND TAPAB=$<TARGET_PROPERTY:${hw_emu_xclbin},FILE_NAME>
            $<TARGET_FILE:vadd-frt> 1000
    DEPENDS vadd-frt ${hw_emu_xclbin}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_custom_target(
    vadd-hw
    COMMAND TAPAB=$<TARGET_PROPERTY:${hw_xclbin},FILE_NAME>
            $<TARGET_FILE:vadd-frt>
    DEPENDS vadd-frt ${hw_xclbin}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

  add_test(NAME vadd-cosim COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                                   --target vadd-cosim)
endif()
