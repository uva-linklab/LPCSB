/* Send an adv and slowly incorporate color code: LPCSB2 */

//Standard Libraries
#include <stdbool.h>
#include <stdint.h>

//Nordic Libraries
#include "app_error.h"
#include "app_timer.h"
#include "app_twi.h"
#include "ble.h"
#include "ble_debug_assert_handler.h"
// #include "boards.h"
#include "led.h"
#include "nordic_common.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf.h"
#include "nrf_sdm.h"
#include "softdevice_handler.h"

//Peripherals
#include "ble_advdata.h"
#include "simple_ble.h"
#include "simple_adv.h"
#include "eddystone.h"

//Sensor Library
#include "tcs3472REDO.h"

/*********************/
/***** LED Stuff *****/
/*********************/
#define LED 17

/*********************/
/****  I2C Stuff *****/
/*********************/
#define I2C_SDA_PIN 29
#define I2C_SCL_PIN 28
#define APP_IRQ_PRIORITY_LOW 3
static app_twi_t twi_instance = APP_TWI_INSTANCE(1);

/***********************/
/***** Timer Stuff *****/
/***********************/
//Timer Ticks appears to be in milliseconds
#define STARTUP_DELAY APP_TIMER_TICKS(10, APP_TIMER_PRESCALER)
#define MEASUREMENT_DELAY APP_TIMER_TICKS(5000, APP_TIMER_PRESCALER) //5000 ms delay
#define APP_TIMER_PRESCALER 0
APP_TIMER_DEF(startup_timer);   //APP_TIMER_TICKS converts ms to timer ticks with prescaler
APP_TIMER_DEF(color_timer);
/************************/
/***** Sensor Stuff *****/
/************************/
uint16_t redData;
uint16_t greenData;
uint16_t blueData;
uint16_t colorTempData; //Not used at the moment
uint16_t luxData;

float maxRatio;
float minRatio;
float ratioCompare;

//Sensor values: Can only transmit bytes, not ints
static struct {
    uint8_t sensorID;
    uint8_t clearTempL; //Leftmost byte
    uint8_t clearTempR; //Rightmost byte
    uint8_t redTempL;   //Leftmost byte
    uint8_t redTempR;   //Rightmost byte
    uint8_t greenTempL; //Leftmost byte
    uint8_t greenTempR; //Rightmost byte
    uint8_t blueTempL;  //Leftmost byte
    uint8_t blueTempR;  //Rightmost byte
    uint8_t colorTempL; //Leftmost byte
    uint8_t colorTempR; //Rightmost byte
    uint8_t luxL;       //Leftmost byte
    uint8_t luxR;       //Rightmost byte
    uint8_t packetNumL; //Packet number MSB
    uint8_t packetNumR; //Packet number LSB
} color_sensor_info = {0};

//Identification:
static struct {
    uint8_t sensorID;
    uint8_t LightType;  //Type of light: 0's = Incandescent, 1's = LED, 2's = Fluorescent, 3's = Sunlight, 4's = Unknown
    uint8_t packetNumL; //Packet number MSB
    uint8_t packetNumR; //Packet number LSB
} light_type = {0};

//Configuration indicators
static struct{
    bool intTimeConfigured;
    bool gainConfigured;
    bool sensorEnabled;
    bool adcEnabled;
    bool interruptSet;
} register_configuration = {0};

/*************************************/
/********** BLE Advertising **********/
/*************************************/
#define BLE_ADVERTISING_ENABLED 1
#define DEVICE_NAME             "LPCSB_TEST"
#define COLOR_DATA_URL          "j2x.us/LPCSB"
#define UVA_COMPANY_IDENTIFIER  0x02E0
#define UVA_RAW_COLOR_SERVICE   0x31
#define UVA_LIGHT_COLOR_SERVICE 0x32

uint8_t color_data[1 + sizeof(color_sensor_info)];
uint8_t light_data[1 + sizeof(light_type)];

// Intervals for advertising and connections
static simple_ble_config_t ble_config = {
    .platform_id       = 0x40,              // used as 4th octect in device BLE address
    .device_id         = DEVICE_ID_DEFAULT,
    .adv_name          = DEVICE_NAME,       // used in advertisements if there is room
    .adv_interval      = MSEC_TO_UNITS(2500, UNIT_0_625_MS), //Adv. interval of 5000 ms
    .min_conn_interval = MSEC_TO_UNITS(500, UNIT_1_25_MS),
    .max_conn_interval = MSEC_TO_UNITS(1000, UNIT_1_25_MS)
};

/*************************************/
/******** Function Prototypes ********/
/*************************************/
static void i2c_init (void);
static void start_sensing();
static void finish_reading_ID (int8_t ID);
static void finish_set_int_time();
static void finish_set_gain();
static void finish_sensor_enable();
static void finish_set_interrupt();
static void finish_reading_clear (int16_t clear);
static void finish_reading_red (int16_t red);
static void finish_reading_green (int16_t green);
static void finish_reading_blue (int16_t blue);
static void processData();
static void advertiseData();
static void start_color_measuring();

/*************************************/
/***** Reading and Config Methods ****/
/*************************************/
static void start_color_measuring(){
    tcs34725_read_clear(finish_reading_clear);
}

static void advertiseData(){
    //If the packet number bytes are at their max values:
    if(color_sensor_info.packetNumR >= 255 || light_type.packetNumR >= 255){
        color_sensor_info.packetNumR = 0;   //Reset the color data packet number LSB
        color_sensor_info.packetNumL += 1;  //Increment the color data packet number MSB
        light_type.packetNumR = 0;          //Reset the light type packet number LSB
        light_type.packetNumL += 1;         //Increment the light type packet number MSB
    }
    else{
        color_sensor_info.packetNumR += 1;  //Increment the color data packet number LSB
        color_sensor_info.packetNumL += 0;  //Leave the color data packet number MSB alone
        light_type.packetNumR += 1;         //Increment the light type packet number LSB
        light_type.packetNumL += 0;         //Leave the light type packet number MSB alone
    }

    ble_advdata_manuf_data_t DataSent;

    //Light type identification
    if(redData >= greenData && redData >= blueData && maxRatio >= 1.1 && minRatio <= 1.1){
        light_type.LightType = 0x00; //Incandescent
    }
    else if(ratioCompare <= 1.45 && luxData <= 2000){
        light_type.LightType = 0x11; //LED
    }
    else if(ratioCompare <= 1.7 && luxData >= 2000){
        light_type.LightType = 0x22; //Fluorescent
    }
    // else if(){} //Daylight characteristics haven't been identified yet
    else{
        light_type.LightType = 0x44; //Unknown
    }

    if(light_type.LightType == 0x44){ //If the type of light is unknown. Transmit the raw data
        color_data[0] = UVA_RAW_COLOR_SERVICE;
        memcpy(color_data + 1, &color_sensor_info, sizeof(color_sensor_info));

        DataSent.company_identifier = UVA_COMPANY_IDENTIFIER;
        DataSent.data.p_data = color_data;
        DataSent.data.size = 1 + sizeof(color_data);

        //Flash the LED very quickly 10 times
        for(int i = 0; i < 10; i++){
            led_toggle(LED);
            nrf_delay_ms(50);
            led_toggle(LED);
            nrf_delay_ms(50);
        }
    }
    else{   //The type of light has been identified! Transmit it
        light_data[0] = UVA_LIGHT_COLOR_SERVICE;
        memcpy(light_data + 1, &light_type, sizeof(light_data));

        DataSent.company_identifier = UVA_COMPANY_IDENTIFIER;
        DataSent.data.p_data = light_data;
        DataSent.data.size = 1 + sizeof(light_data);

        //Flash the LED very quickly 10 times
        for(int i = 0; i < 10; i++){
            led_toggle(LED);
            nrf_delay_ms(50);
            led_toggle(LED);
            nrf_delay_ms(250);
        }
    }
    // Advertise name and data
    // simple_adv_only_name();
    simple_adv_manuf_data(&DataSent);
    // eddystone_with_manuf_adv(COLOR_DATA_URL, &DataSent); //Transmit the data
    led_on(LED);
    nrf_delay_ms(1000);
    led_off(LED);

    app_timer_start(color_timer, MEASUREMENT_DELAY, NULL);
}

static void processData(){
redData = color_sensor_info.redTempL;   //Reset the red variable and set equal to redTemp MSB
    redData = redData << 8;                 //Shift the bits to the left by 8
    redData |= color_sensor_info.redTempR;  //Or the result with redTemp LSB

    greenData = color_sensor_info.greenTempL;   //Reset the green variable and set equal to greenTemp MSB
    greenData = greenData << 8;                 //Shift the bits to the left by 8
    greenData |= color_sensor_info.greenTempR;  //Or the result with greenTemp LSB

    blueData = color_sensor_info.blueTempL;   //Reset the blue variable and set equal to blueTemp MSB
    blueData = blueData << 8;                 //Shift the bits to the left by 8
    blueData |= color_sensor_info.blueTempR;  //Or the result with blueTemp LSB

    colorTempData = color_sensor_info.colorTempL;   //Reset the colorTemp variable and set equal to colorTemp MSB
    colorTempData = colorTempData << 8;             //Shift the bits to the left by 8
    colorTempData |= color_sensor_info.colorTempR;  //Or the result with colorTemp LSB

    luxData = color_sensor_info.luxL;   //Reset the colorTemp variable and set equal to colorTemp MSB
    luxData = luxData << 8;             //Shift the bits to the left by 8
    luxData |= color_sensor_info.luxR;  //Or the result with colorTemp LSB

    maxRatio = 0.0;
    minRatio = 0.0;
    ratioCompare = 0.0;

    if(redData >= greenData && redData >= blueData){    //If red is greater than or equal to green and blue:
        if(greenData >= blueData){
            maxRatio = 1.0 * (float)redData / (float)greenData;
            minRatio = 1.0 * (float)greenData / (float)blueData;
        }
        else{
            maxRatio = 1.0 * (float)redData / (float)blueData;
            minRatio = 1.0 * (float)blueData / (float)greenData;
        }
    }
    else if(greenData >= blueData && greenData >= redData){ //If green is greater than or equal to blue and red:
        if(blueData >= redData){
            maxRatio = 1.0 * (float)greenData / (float)blueData;
            minRatio = 1.0 * (float)blueData / (float)redData;
        }
        else{
            maxRatio = 1.0 * (float)greenData / (float)redData;
            minRatio = 1.0 * (float)redData / (float)blueData;
        }
    }
    else if(blueData >= redData && blueData >= greenData){  //If blue is greater than or equal to red and green:
        if(redData >= greenData){
            maxRatio = 1.0 * (float)blueData / (float)redData;
            minRatio = 1.0 * (float)redData / (float)greenData;
        }
        else{
            maxRatio = 1.0 * (float)blueData / (float)greenData;
            minRatio = 1.0 * (float)greenData / (float)redData;
        }
    }

    if(maxRatio >= minRatio){
        ratioCompare = maxRatio / minRatio;
    }
    else{
        ratioCompare = minRatio / maxRatio;
    }

    advertiseData();
}

static void finish_reading_blue (int16_t blue){
    color_sensor_info.blueTempL = (blue >> 8);
    color_sensor_info.blueTempR = (blue & 0xFF);
    color_sensor_info.colorTempL = (tcs34725_calculate_color_temperature() >> 8);
    color_sensor_info.colorTempR = (tcs34725_calculate_color_temperature() & 0xFF);
    color_sensor_info.luxL = (tcs34725_calculate_lux() >> 8);
    color_sensor_info.luxR = (tcs34725_calculate_lux() & 0xFF);

    processData();
}

static void finish_reading_green (int16_t green){
    color_sensor_info.greenTempL = (green >> 8);
    color_sensor_info.greenTempR = (green & 0xFF);
    tcs34725_read_blue(finish_reading_blue);        //Read the blue-filter photodiode
}

static void finish_reading_red (int16_t red){
    color_sensor_info.redTempL = (red >> 8);
    color_sensor_info.redTempR = (red & 0xFF);

    tcs34725_read_green(finish_reading_green);      //Read the green-filter photodiode

}

static void finish_reading_clear (int16_t clear){
    color_sensor_info.clearTempL = (clear >> 8);
    color_sensor_info.clearTempR = (clear & 0xFF);

    tcs34725_read_red(finish_reading_red);          //Read the red-filter photodiode

}

static void finish_set_interrupt(){

    register_configuration.interruptSet = true;
    tcs34725_read_clear(finish_reading_clear);      //Read the clear-filter photodiode
}

static void finish_adc_enable(){
    register_configuration.adcEnabled = true;

    //Note: for some reason we need both this AND the C-file timer values...
    nrf_delay_ms(700);

    tcs34725_read_clear(finish_reading_clear);      //Read the clear-filter photodiode
}

static void finish_sensor_enable(){
    register_configuration.sensorEnabled = true;
    nrf_delay_ms(3);  
    tcs34725_adc_enable(finish_adc_enable);         //Enable the ADC
}

static void finish_set_gain(){
    register_configuration.gainConfigured = true;
    tcs34725_sensor_enable(finish_sensor_enable);   //Enable the internal oscillator
}

static void finish_set_int_time(){
    register_configuration.intTimeConfigured = true;
    tcs34725_Set_Gain(finish_set_gain);             //Set the gain
}

static void finish_reading_ID (int8_t ID){
    color_sensor_info.sensorID = ID;
    light_type.sensorID = ID;
    tcs34725_Set_Int_Time(finish_set_int_time);     //Set the integration time
}

//Initialize the TWI bus (I2C bus)
static void i2c_init (void) {
    // Initialize the I2C module
    nrf_drv_twi_config_t twi_config;
    twi_config.sda               = I2C_SDA_PIN;
    twi_config.scl                = I2C_SCL_PIN;
    twi_config.frequency          = NRF_TWI_FREQ_100K;
    twi_config.interrupt_priority = APP_IRQ_PRIORITY_LOW;

    //XXX: 8 is arbitrary!!
    uint32_t err_code;
    APP_TWI_INIT(&twi_instance, &twi_config, 8, err_code);
    APP_ERROR_CHECK(err_code);
}

// Set up the sensor configurations and start sampling
static void start_sensing () {
    tcs34725_init(&twi_instance);           //Initialize the sensor
    tcs34725_read_ID(finish_reading_ID);    //Read the ID of the sensor (to check connection)
}

int main(void) {
    uint32_t err_code;

    /* All methods are bracketed by an LED turning on and 
     * off to show that the method has been initialized. */

    /* Initialize the LED for the BLE */
    led_init(LED);
    led_off(LED);

    //Reset the packet number for the color sensor data
    color_sensor_info.packetNumL = 0;
    color_sensor_info.packetNumR = 0;
    light_type.packetNumL = 0;
    light_type.packetNumR = 0;

    // Setup BLE (this also inits the timer AND softdevice libraries)
    led_on(LED);
    nrf_delay_ms(1000);
    simple_ble_init(&ble_config);
    led_off(LED);
    nrf_delay_ms(1000);

    //Initialize the I2C channels
    led_on(LED);
    nrf_delay_ms(1000);
    i2c_init();
    led_off(LED);
    nrf_delay_ms(1000);

    //Create the timers
    led_on(LED);
    nrf_delay_ms(1000);
    app_timer_create(&startup_timer, APP_TIMER_MODE_SINGLE_SHOT, start_sensing);
    app_timer_create(&color_timer, APP_TIMER_MODE_SINGLE_SHOT, start_color_measuring);
    led_off(LED);
    nrf_delay_ms(1000);

    //Start and run this timer for STARTUP_DELAY milliseconds
    app_timer_start(startup_timer, STARTUP_DELAY, NULL);

    while (1) {
        power_manage();
    }
}
