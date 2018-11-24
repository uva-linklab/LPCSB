
PROJECT_NAME = $(shell basename "$(realpath ./)")

APPLICATION_SRCS = $(notdir $(wildcard ./*.c))
# Various C libraries that need to be included
APPLICATION_SRCS += softdevice_handler.c
APPLICATION_SRCS += ble_advdata.c
APPLICATION_SRCS += ble_conn_params.c
APPLICATION_SRCS += app_timer.c
APPLICATION_SRCS += ble_srv_common.c
APPLICATION_SRCS += app_util_platform.c
APPLICATION_SRCS += nrf_drv_common.c
APPLICATION_SRCS += nrf_delay.c
APPLICATION_SRCS += led.c
APPLICATION_SRCS += app_error.c
APPLICATION_SRCS += nrf_drv_twi.c


# Add other libraries here!
APPLICATION_SRCS += simple_ble.c
APPLICATION_SRCS += eddystone.c
APPLICATION_SRCS += simple_adv.c
APPLICATION_SRCS += multi_adv.c
APPLICATION_SRCS += app_twi.c

# platform-level headers and source files
LIBRARY_PATHS += /home/cynthia/color/software/include
SOURCE_PATHS += /home/cynthia/color/software/src

SDK_VERSION = 11

# Set the softdevice needed for the application
SOFTDEVICE_MODEL = s130

# Include the main Makefile
NRF_BASE_PATH ?= /home/cynthia/color/software/nrf5x-base
include $(NRF_BASE_PATH)/make/Makefile
