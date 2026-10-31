# SPDX-License-Identifier: Apache-2.0
# Keep the pinned Cortex-M template authoritative. Its unguarded RAM_SIZE
# definition uses whole KiB; generate one build-local input with the exact
# devicetree byte extent instead. No upstream file or maintained copy is edited.
function(gmk70_configure_linker input output)
  # These allocators derive their upper bound from whole-KiB Kconfig values.
  # The current port has no runtime libc heap. Reject an incompatible future
  # configuration instead of permitting a negative/underflowed heap extent.
  if(CONFIG_NEWLIB_LIBC OR
     (DEFINED CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE AND
      CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE LESS 0))
    message(FATAL_ERROR "WB32 byte RAM extent requires an explicit bounded libc heap")
  endif()
  file(READ "${input}" template)
  set(definition "#define RAM_SIZE (CONFIG_SRAM_SIZE * 1K)")
  string(REGEX MATCHALL "#define RAM_SIZE \\(CONFIG_SRAM_SIZE \\* 1K\\)" definitions "${template}")
  list(LENGTH definitions count)
  if(NOT count EQUAL 1)
    message(FATAL_ERROR "Pinned Cortex-M RAM_SIZE definition changed; review the WB32 RAM adapter")
  endif()
  string(REPLACE "${definition}"
    "#define RAM_SIZE DT_REG_SIZE(DT_CHOSEN(zephyr_sram))"
    template "${template}")
  string(APPEND template "
/* GMK70 retained records occupy the first and last 64 bytes of 28 KiB. */
__gmk_ram_start = ORIGIN(RAM);
__gmk_ram_end = ORIGIN(RAM) + LENGTH(RAM);
ASSERT(__gmk_ram_start == 0x20000040, \"GMK70 lower retained RAM overlaps linker region\")
ASSERT(__gmk_ram_end == 0x20006fc0, \"GMK70 upper retained RAM overlaps linker region\")
ASSERT(__kernel_ram_end == __gmk_ram_end, \"GMK70 kernel RAM extent differs from linker region\")
ASSERT(_image_ram_end <= __gmk_ram_end, \"GMK70 image exceeds admitted RAM\")
")
  # configure_file-style generation updates only on content changes, so a
  # subsequent Ninja configure neither rewrites dependencies nor loops.
  file(CONFIGURE OUTPUT "${output}" CONTENT "${template}" @ONLY)
  if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
  endif()
endfunction()
