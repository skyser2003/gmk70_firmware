# SPDX-License-Identifier: Apache-2.0
zephyr_include_directories(.)
zephyr_sources(soc.c)
include(${CMAKE_CURRENT_LIST_DIR}/linker.cmake)
set(gmk_linker_script ${CMAKE_CURRENT_BINARY_DIR}/wb32fq95-linker.ld)
gmk70_configure_linker(
  ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_m/scripts/linker.ld
  ${gmk_linker_script})
set(SOC_LINKER_SCRIPT ${gmk_linker_script} CACHE INTERNAL "" FORCE)
