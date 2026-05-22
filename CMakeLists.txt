# SPDX-License-Identifier: MIT

set(zephyr_driver_sources ${zephyr_driver_sources} src/encoder_custom.c PARENT_SCOPE)
zephyr_include_directories(${CMAKE_SOURCE_DIR}/include)
