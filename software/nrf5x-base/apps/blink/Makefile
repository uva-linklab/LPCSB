PROJECT_NAME = $(shell basename "$(realpath ./)")

APPLICATION_SRCS = $(notdir $(wildcard ./*.c))
APPLICATION_SRCS += softdevice_handler.c
APPLICATION_SRCS += app_util_platform.c
APPLICATION_SRCS += app_timer.c
APPLICATION_SRCS += app_error.c
APPLICATION_SRCS += app_error_weak.c
APPLICATION_SRCS += nrf_drv_common.c
APPLICATION_SRCS += nrf_drv_clock.c
APPLICATION_SRCS += led.c

NRF_MODEL = nrf51

SDK_VERSION = 12
SOFTDEVICE_MODEL = s130

NRF_BASE_PATH ?= /home/alexander/Desktop/Programming_nrf51822_boards/nrf5x-base

LIBRARY_PATHS += /home/alexander/Desktop/Programming_nrf51822_boards/include
SOURCE_PATHS += /home/alexander/Desktop/Programming_nrf51822_boards/src

include $(NRF_BASE_PATH)/make/Makefile
