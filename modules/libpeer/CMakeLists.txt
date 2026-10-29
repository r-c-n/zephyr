# Copyright (c) 2026 Igalia S.L.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_LIBPEER)

zephyr_library_named(libpeer)

zephyr_include_directories(
  ${ZEPHYR_LIBPEER_MODULE_DIR}/src
  ${ZEPHYR_LIBPEER_MODULE_DIR}/third_party/cJSON/
)

file(GLOB LIBPEER_SRC ${ZEPHYR_LIBPEER_MODULE_DIR}/src/*.c)
# These files have Zephyr-specific implementations:
# - peer_signaling_zephyr.c
# - mdns_zephyr.c
# So use those instead
list(FILTER LIBPEER_SRC EXCLUDE REGEX "peer_signaling.c")
list(FILTER LIBPEER_SRC EXCLUDE REGEX "mdns.c")

zephyr_library_sources(${LIBPEER_SRC})
zephyr_library_sources(
  ${ZEPHYR_LIBPEER_MODULE_DIR}/third_party/cJSON/cJSON.c
)

zephyr_compile_definitions(
  CONFIG_SDP_BUFFER_SIZE=4096

  # TODO: parameterize this according to the architecture
  __BYTE_ORDER=__LITTLE_ENDIAN

  # Enable Zephyr build
  CONFIG_USE_ZEPHYR=1

  # Disabled features
  CONFIG_USE_LWIP=0
  CONFIG_USE_USRSCTP=0
  HTTP_DO_NOT_USE_CUSTOM_CONFIG
  MQTT_DO_NOT_USE_CUSTOM_CONFIG

  # Mbedtls config
  CONFIG_MBEDTLS_2_X=0
)

endif()
