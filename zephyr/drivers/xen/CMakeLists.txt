# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)
zephyr_library()
zephyr_library_sources(xenstore_client.c)
zephyr_library_sources_ifdef(CONFIG_EPP_XEN_NETFRONT netfront.c)
