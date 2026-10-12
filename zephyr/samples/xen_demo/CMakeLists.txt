# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xen_demo)

# The parts register themselves in an iterable section (src/part.h)
zephyr_linker_sources(SECTIONS src/parts.ld)

target_sources(app PRIVATE src/main.c)
target_sources_ifdef(CONFIG_I2C app PRIVATE src/i2c_scan.c)

# One file per part, compiled in with its Kconfig option (Kconfig)
target_sources_ifdef(CONFIG_EPP_LPI2C4 app PRIVATE src/i2c_display.c)
if(CONFIG_EPP_LPI2C5 OR CONFIG_EPP_LPI2C6 OR CONFIG_EPP_LPI2C7)
  target_sources(app PRIVATE src/i2c_header.c)
endif()
target_sources_ifdef(CONFIG_EPP_PCF8574 app PRIVATE src/pcf8574.c)
target_sources_ifdef(CONFIG_EPP_SPI_LOOPBACK app PRIVATE src/spi_loopback.c)
target_sources_ifdef(CONFIG_EPP_W5500 app PRIVATE src/w5500_test.c)
target_sources_ifdef(CONFIG_EPP_W5500_ETH app PRIVATE src/w5500_eth.c)
target_sources_ifdef(CONFIG_EPP_FLEXCAN2 app PRIVATE src/can.c)
target_sources_ifdef(CONFIG_EPP_LPUART7 app PRIVATE src/uart.c)
