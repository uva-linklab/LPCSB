/* Color Sensor and Blink Program */

//Standard Libraries
#include <stdbool.h>
#include <stdint.h>
#include "led.h"

//Nordic Libraries
#include "nrf.h"
#include "nrf_sdm.h"
#include "app_twi.h"
#include "app_timer.h"
#include "app_error.h"
#include "nrf_delay.h"
#include "softdevice_handler.h"
#include "ble.h"
#include "nrf_drv_config.h"

//Peripherals
#include "simple_ble.h"
#include "simple_adv.h"
#include "multi_adv.h"
#include "eddystone.h"

#include "tcs3472REDO.h"


/**********************************/
/*** LED, Sensor, and I2C Stuff ***/
/**********************************/
#define LED 17

#define I2C_SDA_PIN 29
#define I2C_SCL_PIN 28

#define APP_IRQ_PRIORITY_LOW 3

#define APP_TIMER_PRESCALER 0

static struct {
    uint16_t sensorID;
    uint16_t redTemp;
    uint16_t greenTemp;
    uint16_t blueTemp;
    uint16_t clearTemp;
    uint16_t colorTemp;
    uint16_t lux;
} color_sensor_info = {0};

static struct{
    bool intTimeConfigured;
    bool gainConfigured;
    bool sensorEnabled;
    bool adcEnabled;
    bool interruptSet;

} register_configuration = {0};

//APP_TIMER_TICKS converts ms to timer ticks with prescaler
APP_TIMER_DEF(startup_timer);
#define STARTUP_DELAY APP_TIMER_TICKS(10, APP_TIMER_PRESCALER)

static app_twi_t twi_instance = APP_TWI_INSTANCE(1);

static void i2c_init (void);
static void start_sensing();
static void finish_reading_ID (int8_t ID);
static void finish_set_int_time();
static void finish_set_gain();
static void finish_sensor_enable();
static void finish_set_interrupt();

static void finish_reading_red (uint16_t red);
static void finish_reading_green (uint16_t green);
static void finish_reading_blue (uint16_t blue);
static void finish_reading_clear (uint16_t clear);

// Intervals for advertising and connections (for transmission)
//static simple_ble_config_t ble_config = {
 //   .platform_id       = 0x30,              // used as 4th octect in device BLE address
 //   .device_id         = DEVICE_ID_DEFAULT,
 //   .adv_name          = DEVICE_NAME,       // used in advertisements if there is room
 //   .adv_interval      = APP_ADV_INTERVAL,
  //  .min_conn_interval = MIN_CONN_INTERVAL,
  //  .max_conn_interval = MAX_CONN_INTERVAL,
//};

static void finish_reading_blue (uint16_t blue){
    color_sensor_info.blueTemp = blue;
    //color_sensor_info.colorTemp = tcs34725_calculate_color_temperature();
    //color_sensor_info.lux = tcs34725_calculate_lux();
    //led_toggle(LED);
}

static void finish_reading_green (uint16_t green){
    color_sensor_info.greenTemp = green;
    tcs34725_read_blue(finish_reading_blue);    
}

static void finish_reading_red (uint16_t red){
    color_sensor_info.redTemp = red;
    tcs34725_read_green(finish_reading_green);

}

static void finish_reading_clear (uint16_t clear){
    color_sensor_info.clearTemp = clear;
    tcs34725_read_red(finish_reading_red);
}

static void finish_set_interrupt(){
    register_configuration.interruptSet = true;
    tcs34725_read_clear(finish_reading_clear);
}

static void finish_adc_enable(){
    register_configuration.adcEnabled = true;

    //Note: for some reason we need both this AND the C-file timer values...
    nrf_delay_ms(700);
    
    tcs34725_set_Interrupt(finish_set_interrupt);
}

static void finish_sensor_enable(){
    register_configuration.sensorEnabled = true;
    nrf_delay_ms(3);  
    tcs34725_adc_enable(finish_adc_enable);
}

static void finish_set_gain(){
    register_configuration.gainConfigured = true;
    tcs34725_sensor_enable(finish_sensor_enable);
}

static void finish_set_int_time(){
    register_configuration.intTimeConfigured = true;
    tcs34725_Set_Gain(finish_set_gain);
}

static void finish_reading_ID (int8_t ID){
    color_sensor_info.sensorID = ID;
    tcs34725_Set_Int_Time(finish_set_int_time);
}

//Initialize the TWI bus (I2C bus)
static void i2c_init (void) {

    // Initialize the I2C module
    nrf_drv_twi_config_t twi_config;
    twi_config.sda                = I2C_SDA_PIN;
    twi_config.scl                = I2C_SCL_PIN;
    twi_config.frequency          = NRF_TWI_FREQ_100K;
    twi_config.interrupt_priority = APP_IRQ_PRIORITY_LOW;

    //XXX: 8 is arbitrary!!
    uint32_t err_code;
    APP_TWI_INIT(&twi_instance, &twi_config, 8, err_code);
    APP_ERROR_CHECK(err_code);
}

static void start_sensing () {
    // setup the sensor configurations and start sampling
    led_toggle(LED);
    tcs34725_init(&twi_instance);
    tcs34725_read_ID(finish_reading_ID);
}

int main(void) {

    /* Initialize the LED for the BLE */
    led_init(LED);
    led_off(LED);

    // Need to set the clock to something
    nrf_clock_lf_cfg_t clock_lf_cfg = {
        .source        = NRF_CLOCK_LF_SRC_RC,
        .rc_ctiv       = 16,
        .rc_temp_ctiv  = 2,
        .xtal_accuracy = NRF_CLOCK_LF_XTAL_ACCURACY_250_PPM};

    // Initialize the SoftDevice handler module.
    SOFTDEVICE_HANDLER_INIT(&clock_lf_cfg, NULL);

    led_toggle(LED);
    nrf_delay_ms(1000);
    led_toggle(LED);
    nrf_delay_ms(1000);

    //Initialize the I2C channels
    i2c_init();

    led_toggle(LED);
    nrf_delay_ms(1000);
    led_toggle(LED);
    nrf_delay_ms(1000);

    //Initialize timer library (MUST do to make timers work)
    APP_TIMER_INIT(APP_TIMER_PRESCALER, 5, false);

    led_toggle(LED);
    nrf_delay_ms(1000);
    led_toggle(LED);
    nrf_delay_ms(1000);

    // Delay until hardware is ready
    app_timer_create(&startup_timer, APP_TIMER_MODE_SINGLE_SHOT, start_sensing);

    led_toggle(LED);
    nrf_delay_ms(1000);
    led_toggle(LED);
    nrf_delay_ms(1000);

    app_timer_start(startup_timer, STARTUP_DELAY, NULL);
 	
    // Setup BLE
    //simple_ble_init(&ble_config);
    
    // Enter main loop.
    while (1) {
        power_manage();
    }
}
