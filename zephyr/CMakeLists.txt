# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_EPP_XEN_XENSTORE drivers/xen)
add_subdirectory_ifdef(CONFIG_EPP_XEN_RGPIO drivers/gpio)
add_subdirectory_ifdef(CONFIG_EPP_XEN_LPI2C drivers/i2c)
add_subdirectory_ifdef(CONFIG_EPP_XEN_LPSPI drivers/spi)
add_subdirectory_ifdef(CONFIG_EPP_XEN_FLEXCAN drivers/can)
