/* Copyright (c) 2013 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

/**@file
 *
 * @defgroup ble_sdk_app_bootloader_main main.c
 * @{
 * @ingroup dfu_bootloader_api
 * @brief Bootloader project main file.
 *
 * -# Receive start data packet.
 * -# Based on start packet, prepare NVM area to store received data.
 * -# Receive data packet.
 * -# Validate data packet.
 * -# Write Data packet to NVM.
 * -# If not finished - Wait for next packet.
 * -# Receive stop data packet.
 * -# Activate Image, boot application.
 *
 */
#include "bootloader.h"
#include "bootloader_util.h"
#include "bootloader_settings.h"
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "nordic_common.h"
#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_soc.h"
#include "app_error.h"
#include "nrf_gpio.h"
#include "nrf51_bitfields.h"
#include "ble.h"
#include "nrf51.h"
#include "ble_hci.h"
#include "app_scheduler.h"
#include "app_timer_appsh.h"
#include "nrf_error.h"
#include "softdevice_handler_appsh.h"
#include "pstorage_platform.h"
#include "nrf_mbr.h"
#include "boards.h"

// #if BUTTONS_NUMBER < 1
// #error "Not enough buttons on board"
// #endif

// #if LEDS_NUMBER < 1
// #error "Not enough LEDs on board"
// #endif

#define IS_SRVC_CHANGED_CHARACT_PRESENT 1                                                       /**< Include the service_changed characteristic. For DFU this should normally be the case. */

// #define BOOTLOADER_BUTTON               BSP_BUTTON_3                                         /**< Button used to enter SW update mode. */
#define UPDATE_IN_PROGRESS_LED          ERROR_LED                                                      /**< Led used to indicate that DFU is active. */

#define APP_TIMER_PRESCALER             0                                                       /**< Value of the RTC1 PRESCALER register. */

#define APP_TIMER_OP_QUEUE_SIZE         4                                                       /**< Size of timer operation queues. */

#define BUTTON_DETECTION_DELAY          APP_TIMER_TICKS(50, APP_TIMER_PRESCALER)                /**< Delay from a GPIOTE event until a button is reported as pushed (in number of timer ticks). */

#define SCHED_MAX_EVENT_DATA_SIZE       MAX(APP_TIMER_SCHED_EVT_SIZE, 0)                        /**< Maximum size of scheduler events. */

#define SCHED_QUEUE_SIZE                20                                                      /**< Maximum number of events in the scheduler queue. */

//static const uint8_t ble_addr[6] __attribute__ ((section(".noInit")));
#define BOOTLOADER_BLE_ADDR_START 0x20007F80

#ifndef SOFTDEVICE_s130
void app_error_handler(uint32_t error_code, uint32_t line_num, const uint8_t * p_file_name) {
#else
void app_error_fault_handler(uint32_t error_code, uint32_t line_num, uint32_t info) {
#endif
  nrf_gpio_pin_clear(UPDATE_IN_PROGRESS_LED);
  while(1) {
    nrf_gpio_pin_clear(UPDATE_IN_PROGRESS_LED);
    nrf_delay_ms(100);
    nrf_gpio_pin_set(UPDATE_IN_PROGRESS_LED);
    nrf_delay_ms(100);
  };
}

/**@brief Callback function for asserts in the SoftDevice.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num    Line number of the failing ASSERT call.
 * @param[in] file_name   File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
#ifndef SOFTDEVICE_s130
    app_error_handler(0xDEADBEEF, line_num, p_file_name);
#else
    app_error_fault_handler(0xDEADBEEF, line_num, 0);
#endif
}


/**@brief Function for initialization of LEDs.
 */
static void leds_init(void)
{
    nrf_gpio_cfg_output(UPDATE_IN_PROGRESS_LED);
    nrf_gpio_pin_set(UPDATE_IN_PROGRESS_LED);
}


/**@brief Function for initializing the timer handler module (app_timer).
 */
static void timers_init(void)
{
    // Initialize timer module, making it use the scheduler.
    APP_TIMER_APPSH_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, true);
}


/**@brief Function for initializing the button module.
 */
static void buttons_init(void)
{
    // nrf_gpio_cfg_sense_input(BOOTLOADER_BUTTON,
    //                          BUTTON_PULL,
    //                          NRF_GPIO_PIN_SENSE_LOW);

}


/**@brief Function for dispatching a BLE stack event to all modules with a BLE stack event handler.
 *
 * @details This function is called from the scheduler in the main loop after a BLE stack
 *          event has been received.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 */
static void sys_evt_dispatch(uint32_t event)
{
    pstorage_sys_event_handler(event);
}


/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 *
 * @param[in] init_softdevice  true if SoftDevice should be initialized. The SoftDevice must only
 *                             be initialized if a chip reset has occured. Soft reset from
 *                             application must not reinitialize the SoftDevice.
 */
static void ble_stack_init(bool init_softdevice)
{
    uint32_t         err_code;
    const uint8_t*   _ble_addr;
    sd_mbr_command_t com = {SD_MBR_COMMAND_INIT_SD, };

    if (init_softdevice)
    {
        err_code = sd_mbr_command(&com);
        APP_ERROR_CHECK(err_code);
    }

    err_code = sd_softdevice_vector_table_base_set(BOOTLOADER_REGION_START);
    APP_ERROR_CHECK(err_code);


#ifdef SOFTDEVICE_s130
    // Softdevice 130 2.0.0 changes how the softdevice init procedure works.
    nrf_clock_lf_cfg_t clock_lf_cfg = {
        .source        = NRF_CLOCK_LF_SRC_RC,
        .rc_ctiv       = 16, // bradjc: I mostly made these up based on docs. May be not great.
        .rc_temp_ctiv  = 2,
        .xtal_accuracy = NRF_CLOCK_LF_XTAL_ACCURACY_250_PPM};

    SOFTDEVICE_HANDLER_APPSH_INIT(&clock_lf_cfg, true);

    ble_enable_params_t ble_enable_params;
    err_code = softdevice_enable_get_default_config(0, // central link count
                                                    1, // peripheral link count
                                                    &ble_enable_params);
    APP_ERROR_CHECK(err_code);

    //Check the ram settings against the used number of links
    CHECK_RAM_START_ADDR(CENTRAL_LINK_COUNT, PERIPHERAL_LINK_COUNT);

    // Enable BLE stack.
    err_code = softdevice_enable(&ble_enable_params);
    APP_ERROR_CHECK(err_code);
#else
    SOFTDEVICE_HANDLER_APPSH_INIT(NRF_CLOCK_LFCLKSRC_RC_250_PPM_250MS_CALIBRATION, true);
    // Enable BLE stack
    ble_enable_params_t ble_enable_params;
    memset(&ble_enable_params, 0, sizeof(ble_enable_params));
    ble_enable_params.gatts_enable_params.service_changed = IS_SRVC_CHANGED_CHARACT_PRESENT;
    err_code = sd_ble_enable(&ble_enable_params);
    APP_ERROR_CHECK(err_code);
#endif

    err_code = softdevice_sys_evt_handler_set(sys_evt_dispatch);
    APP_ERROR_CHECK(err_code);

    // Set the MAC address of the device
    // Highest priority is address set by application in bootloader_settings
    // Next is address from flash if available
    // Nordic assigned random value is used as last choice
    ble_gap_addr_t gap_addr;
      // No application-defined address stored in bootloader_settings
      // get BLE address from Flash
      _ble_addr = (uint8_t*)BLEADDR_FLASH_LOCATION;
      if (_ble_addr[1] == 0xFF && _ble_addr[0] == 0xFF) {
          // get BLE address from shared RAM
          _ble_addr = (uint8_t*)BOOTLOADER_BLE_ADDR_START;
          if (_ble_addr[1] == 0x00 && _ble_addr[0] == 0x00) {
            // No user-defined address stored in flash or shared RAM

            // New address is a combination of Michigan OUI and Platform ID
            uint8_t new_mac_addr[6] = {0x00, 0x00, PLATFORM_ID_BYTE, 0xe5, 0x98, 0xc0};

            // Set the new BLE address with the Michigan OUI, Platform ID, and
            //  bottom two octets from the original gap address
            // Get the current original address
            sd_ble_gap_address_get(&gap_addr);
            memcpy(gap_addr.addr+2, new_mac_addr+2, sizeof(gap_addr.addr)-2);
          } else {
            // Set the new BLE address with the address in shared RAM
            memcpy(gap_addr.addr, _ble_addr, 6);
          }
    } else {
      memcpy(gap_addr.addr, _ble_addr, 6);
    }

    gap_addr.addr_type = BLE_GAP_ADDR_TYPE_PUBLIC;
    err_code = sd_ble_gap_address_set(BLE_GAP_ADDR_CYCLE_MODE_NONE, &gap_addr);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for event scheduler initialization.
 */
static void scheduler_init(void)
{
    APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE);
}


/**@brief Function for bootloader main entry.
 */
int main(void)
{
    uint32_t err_code;
    bool     dfu_start = false;
    bool     app_reset = (NRF_POWER->GPREGRET == BOOTLOADER_DFU_START);
    bool     second_power_cycle = (NRF_POWER->GPREGRET == (BOOTLOADER_DFU_START+1));

    if (app_reset)
    {
        NRF_POWER->GPREGRET = BOOTLOADER_DFU_START+1;
        NVIC_SystemReset();
    }

    if (second_power_cycle) {
        NRF_POWER->GPREGRET = 0;
    }


    // This check ensures that the defined fields in the bootloader corresponds with actual
    // setting in the nRF51 chip.
    APP_ERROR_CHECK_BOOL(*((uint32_t *)NRF_UICR_BOOT_START_ADDRESS) == BOOTLOADER_REGION_START);
    APP_ERROR_CHECK_BOOL(NRF_FICR->CODEPAGESIZE == CODE_PAGE_SIZE);

    // Initialize.
    timers_init();
    buttons_init();
    leds_init();

    (void)bootloader_init();

    if (bootloader_dfu_sd_in_progress())
    {
        //nrf_gpio_pin_clear(UPDATE_IN_PROGRESS_LED);

        err_code = bootloader_dfu_sd_update_continue();
        APP_ERROR_CHECK(err_code);

        ble_stack_init(!app_reset);
        scheduler_init();

        err_code = bootloader_dfu_sd_update_finalize();
        APP_ERROR_CHECK(err_code);

        //nrf_gpio_pin_set(UPDATE_IN_PROGRESS_LED);
    }
    else
    {
        // If stack is present then continue initialization of bootloader.
        ble_stack_init(!app_reset);
        scheduler_init();
    }



    // dfu_start  = app_reset;
    dfu_start  = second_power_cycle;
    // dfu_start |= ((nrf_gpio_pin_read(BOOTLOADER_BUTTON) == 0) ? true: false);

    if (dfu_start || (!bootloader_app_is_valid(DFU_BANK_0_REGION_START)))
    {
        nrf_gpio_pin_clear(UPDATE_IN_PROGRESS_LED);

        // Initiate an update of the firmware.
        err_code = bootloader_dfu_start();
        APP_ERROR_CHECK(err_code);

        nrf_gpio_pin_set(UPDATE_IN_PROGRESS_LED);
    }

    if (bootloader_app_is_valid(DFU_BANK_0_REGION_START) && !bootloader_dfu_sd_in_progress())
    {
        // Select a bank region to use as application region.
        // @note: Only applications running from DFU_BANK_0_REGION_START is supported.
        bootloader_app_start(DFU_BANK_0_REGION_START);
    }

    NVIC_SystemReset();
}
