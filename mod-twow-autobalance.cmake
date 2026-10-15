# Optional CMake hooks for mod-twow-autobalance.
#
# Sources under src/ are discovered automatically, conf/*.conf.dist is copied automatically,
# and Addmod_twow_autobalanceScripts() is invoked by the generated module loader.
#
# This file is included twice:
#   TORTOISE_MODULE_CMAKE_PHASE=DISCOVERY
#   TORTOISE_MODULE_CMAKE_PHASE=POST_TARGETS
#
# Use TW_* helpers for compatibility with this core's module system. Do not use AzerothCore AC_* names.

if(TORTOISE_MODULE_CMAKE_PHASE STREQUAL "DISCOVERY")
  # Module discovery phase
endif()

if(TORTOISE_MODULE_CMAKE_PHASE STREQUAL "POST_TARGETS")
  # Post-targets configuration phase
endif()

# NOTE: Compilation is handled exclusively by the operator/user.
# Any optional test targets or standalone builds should be OFF by default.
