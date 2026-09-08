# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BUILD_ONLY_NO_BLOBS)
  message(WARNING "
  ---------------------------------------------------------------------------
  Building only the Bluetooth driver without binary blobs and patches.
  This is only for building (CI) purposes and will not work on a real device.
  ---------------------------------------------------------------------------
  ")
else()

if(CONFIG_DT_HAS_ESPRESSIF_ESP32_BT_HCI_ENABLED)
  zephyr_blobs_verify(MODULE hal_espressif REQUIRED)
endif()

if((CONFIG_DT_HAS_ST_HCI_STM32WBA_ENABLED) OR (CONFIG_DT_HAS_ST_HCI_STM32WB0_ENABLED))
  zephyr_blobs_verify(MODULE hal_stm32 REQUIRED)
endif()

if(CONFIG_DT_HAS_SILABS_BT_HCI_EFR32_ENABLED)
  zephyr_blobs_verify(MODULE hal_silabs REQUIRED)
endif()

if(CONFIG_DT_HAS_INFINEON_BLESS_HCI_ENABLED)
  zephyr_blobs_verify(MODULE hal_infineon REQUIRED)
endif()

if(CONFIG_DT_HAS_RENESAS_BT_HCI_DA1469X_ENABLED)
  zephyr_blobs_verify(MODULE hal_renesas REQUIRED)
endif()

if(CONFIG_BT_BFLB)
  zephyr_blobs_verify(MODULE hal_bouffalolab REQUIRED)
endif()

endif() # CONFIG_BUILD_ONLY_NO_BLOBS

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_BT_AMBIQ_HCI hci_ambiq.c apollox_blue.c)
zephyr_library_sources_ifdef(CONFIG_BT_AMEBA hci_rtk_ameba.c)
zephyr_library_sources_ifdef(CONFIG_BT_BEE hci_bee.c)
zephyr_library_sources_ifdef(CONFIG_BT_BFLB hci_bflb.c)
zephyr_library_sources_ifdef(CONFIG_BT_CYW208XX hci_infineon_cyw208xx.c)
zephyr_library_sources_ifdef(CONFIG_BT_DA1453X hci_da1453x.c)
zephyr_library_sources_ifdef(CONFIG_BT_DA1469X hci_da1469x.c)
zephyr_library_sources_ifdef(CONFIG_BT_ESP32 hci_esp32.c)
zephyr_library_sources_ifdef(CONFIG_BT_EXT_ADV util_hci_evt.c)
zephyr_library_sources_ifdef(CONFIG_BT_H4 h4.c)
zephyr_library_sources_ifdef(CONFIG_BT_H4_NXP_CTLR hci_nxp_setup.c)
zephyr_library_sources_ifdef(CONFIG_BT_H5 h5.c)
zephyr_library_sources_ifdef(CONFIG_BT_HCI_IPC ipc.c)
zephyr_library_sources_ifdef(CONFIG_BT_HCI_UART_INFINEON hci_uart_infineon.c)
zephyr_library_sources_ifdef(CONFIG_BT_NXP hci_nxp.c)
zephyr_library_sources_ifdef(CONFIG_BT_PSOC6_BLESS hci_infineon_psoc6_bless.c)
zephyr_library_sources_ifdef(CONFIG_BT_SF32LB hci_sf32lb.c)
zephyr_library_sources_ifdef(CONFIG_BT_SILABS_EFR32 hci_silabs_efr32.c)
zephyr_library_sources_ifdef(CONFIG_BT_SILABS_SIWX91X hci_silabs_siwx91x.c)
zephyr_library_sources_ifdef(CONFIG_BT_SPI_BLUENRG hci_spi_st.c)
zephyr_library_sources_ifdef(CONFIG_BT_SPI_ZEPHYR spi.c)
zephyr_library_sources_ifdef(CONFIG_BT_STM32WB0 hci_stm32wb0.c)
zephyr_library_sources_ifdef(CONFIG_BT_STM32WBA hci_stm32wba.c)
zephyr_library_sources_ifdef(CONFIG_BT_STM32_IPM ipm_stm32wb.c)
zephyr_library_sources_ifdef(CONFIG_SOC_NRF5340_CPUAPP nrf53_support.c)
# zephyr-keep-sorted-stop

if(CONFIG_BT_USERCHAN)
  zephyr_library_sources(userchan.c)
  target_sources(native_simulator INTERFACE userchan_bottom.c)
endif()

# CYW43 Bluetooth over the shared gSPI bus (Raspberry Pi Pico W / Pico 2 W).
#
# The transport itself is pico-sdk's cybt_shared_bus, which lives in
# hal_rpi_pico and is written against the georgerobotics cyw43-driver headers;
# cyw43_bt_whd_glue.c supplies the handful of primitives it needs, backed by the
# AIROC/WHD WiFi bus. cyw43_configport.h configures cyw43-driver for BT only,
# so none of its WiFi/SPI/lwIP code is compiled.
if(CONFIG_BT_CYW43_SHARED_BUS)
  set(cyw43_root ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/lib/cyw43-driver)
  set(cybt_root ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/rp2_common/pico_cyw43_driver/cybt_shared_bus)

  if(NOT EXISTS ${cyw43_root}/src/cyw43_btbus.h)
    message(FATAL_ERROR
      "CONFIG_BT_CYW43_SHARED_BUS needs the georgerobotics cyw43-driver "
      "submodule of hal_rpi_pico, which is not checked out. Run:\n"
      "  git -C ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR} submodule update --init lib/cyw43-driver")
  endif()

  zephyr_library_sources(
    hci_cyw43_shared_bus.c
    cyw43_bt_whd_glue.c
    ${cybt_root}/cybt_shared_bus.c
    ${cybt_root}/cybt_shared_bus_driver.c
  )

  # This directory first, so cyw43_config.h picks up our cyw43_configport.h.
  zephyr_library_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${cyw43_root}/src
    ${cyw43_root}/firmware
    ${cybt_root}
  )

  # WHD internals for the backplane accessors, plus airoc_wifi.h.
  set(whd_root ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/whd-expansion/WHD)
  zephyr_library_include_directories(
    ${whd_root}/COMPONENT_WIFI5/inc
    ${whd_root}/COMPONENT_WIFI5/src/include
    ${whd_root}/COMPONENT_WIFI5/src/bus_protocols
    ${whd_root}/COMMON/inc
    ${ZEPHYR_BASE}/drivers/wifi/infineon
  )
endif()
