# LPCSB2 Makefile
PROJECT_NAME = $(shell basename "$(realpath ./)")

#Various C libraries that should be included 
APPLICATION_SRCS = $(notdir $(wildcard ./*.c))
APPLICATION_SRCS += app_error.c
APPLICATION_SRCS += app_timer.c
APPLICATION_SRCS += app_util_platform.c
APPLICATION_SRCS += ble_advdata.c
APPLICATION_SRCS += ble_conn_params.c
APPLICATION_SRCS += ble_srv_common.c
APPLICATION_SRCS += led.c
APPLICATION_SRCS += nrf_delay.c
APPLICATION_SRCS += nrf_drv_common.c
APPLICATION_SRCS += nrf_drv_twi.c
APPLICATION_SRCS += softdevice_handler.c

#Add other libraries here
APPLICATION_SRCS += app_twi.c
APPLICATION_SRCS += simple_ble.c
APPLICATION_SRCS += simple_adv.c
APPLICATION_SRCS += eddystone.c

DEVICE = NRF51

# ifndef TARGET
# 	TARGET = SQUALL
# endif

# TARGET_UPPER = $(shell echo $(TARGET) | tr a-z A-Z)
# TARGET_LOWER = $(shell echo $(TARGET) | tr A-Z a-z)
# BOARD = BOARD_$(TARGET_UPPER)

# APPLICATION_SRCS += $(TARGET_LOWER).c

SOFTDEVICE_MODEL = s110

LIBRARY_PATHS += /home/alexander/Desktop/Programming_nrf51822_boards/include
SOURCE_PATHS += /home/alexander/Desktop/Programming_nrf51822_boards/src

NRF_BASE_PATH ?= /home/alexander/Desktop/Programming_nrf51822_boards/nrf5x-base
include $(NRF_BASE_PATH)/make/Makefile
