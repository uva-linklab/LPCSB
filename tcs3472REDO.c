// TCS34725 Color sensor driver

//***Libraries***
#include <stdint.h>
#include <math.h>

#include "tcs3472REDO.h"
#include "nrf_delay.h"

#include "app_twi.h"
#include "app_timer.h"

#include "led.h"

//Peripherals
#include "ble_advdata.h"
#include "simple_ble.h"
#include "simple_adv.h"

//***Global data***
#define LED 17

// I2C peripheral used for this peripheral
static app_twi_t* twi;

// Measurement Storage
static uint8_t tcsIDval[1] = {0};
static uint8_t readEnable[1] = {0};
static uint8_t clear[2] = {0};
static uint8_t red[2] = {0};
static uint8_t green[2] = {0};
static uint8_t blue[2] = {0};

/* Read the ID of tcs34725 (When initializing) */
static uint8_t const READ_ID_CMD[1] = {TCS34725_COMMAND_BIT | TCS34725_ID};
#define READ_SENSOR_LEN 2
static app_twi_transfer_t const READ_SENSOR[READ_SENSOR_LEN ] = {
    APP_TWI_WRITE(TCS34725_ADDRESS, READ_ID_CMD, 1, APP_TWI_NO_STOP),
    APP_TWI_READ(TCS34725_ADDRESS, tcsIDval, 1, 0),
};

/*TCS34725 CONFIGURATION TRANSACTIONS*/
//Set the integration time of the tcs34725
static uint8_t const SET_INT_TIME_CMDS[2] = {TCS34725_COMMAND_BIT | TCS34725_ATIME, (TCS34725_INTEGRATIONTIME_700MS & 0xFF)};
#define INT_CMD_LEN 1
static app_twi_transfer_t const SET_INT_TIME[INT_CMD_LEN ] = {
    APP_TWI_WRITE(TCS34725_ADDRESS, SET_INT_TIME_CMDS, 2, 0),
};

//Set the gain of the tcs34725
static uint8_t const SET_GAIN_CMDS[2] = {TCS34725_COMMAND_BIT | TCS34725_CONTROL, (TCS34725_GAIN_1X & 0xFF)};
#define GAIN_CMD_LEN 1
static app_twi_transfer_t const SET_GAIN[GAIN_CMD_LEN ] = {
    APP_TWI_WRITE(TCS34725_ADDRESS, SET_GAIN_CMDS, 2, 0),
};

//send command to initialize the tcs34725
static uint8_t const POWER_ON[2] = {TCS34725_COMMAND_BIT | TCS34725_ENABLE, (TCS34725_ENABLE_PON & 0xFF)};
#define POWER_ON_LEN 1
static app_twi_transfer_t const POWER_ON_SENSOR[POWER_ON_LEN] = {
    APP_TWI_WRITE(TCS34725_ADDRESS, POWER_ON, 2, 0),
};

static uint8_t const ENABLE_ADC[2] = {TCS34725_COMMAND_BIT | TCS34725_ENABLE, ((TCS34725_ENABLE_PON | TCS34725_ENABLE_AEN) & 0xFF)};
#define ENABLE_ADC_LEN 1
static app_twi_transfer_t const ENABLE_SENSOR_ADC[ENABLE_ADC_LEN] = {
    APP_TWI_WRITE(TCS34725_ADDRESS, ENABLE_ADC, 2, 0),
};

//NOTE: THIS ASSUMES THAT THE SETINTERRUPT BOOLEAN FOR THE ARDUINO VERSION OF THIS CODE IS TRUE!!!!!!!!
static uint8_t const READ_ENABLE_CMD[1] = {TCS34725_COMMAND_BIT | TCS34725_ENABLE};
//((TCS34725_ENABLE_PON | TCS34725_ENABLE_AEN) & 0xFF) = value just written to the Enable register
static uint8_t const SET_INTERRUPT_CMD[2] = {TCS34725_COMMAND_BIT | TCS34725_ENABLE, ((TCS34725_ENABLE_PON | TCS34725_ENABLE_AEN) & 0xFF) | TCS34725_ENABLE_AIEN};
#define SET_INTERRUPT_LEN 3
static app_twi_transfer_t const SET_INTERRUPT[SET_INTERRUPT_LEN] = {
    APP_TWI_WRITE(TCS34725_ADDRESS, READ_ENABLE_CMD, 1, APP_TWI_NO_STOP),
    APP_TWI_READ(TCS34725_ADDRESS, readEnable, 1, APP_TWI_NO_STOP),
    APP_TWI_WRITE(TCS34725_ADDRESS, SET_INTERRUPT_CMD, 2, 0),
};
//To set interrupt to false, & the AIEN value with the value from the enable register
//on line 68 instead of |

/*TCS34725 MEASUREMENT AND READ TRANSACTIONS*/
//measure clear
static uint8_t const MEAS_CLEAR_CMD[1] = {TCS34725_COMMAND_BIT | TCS34725_CDATAL};
#define MEAS_CLEAR_TXFR_LEN 2
static app_twi_transfer_t const MEAS_CLEAR_TXFR[MEAS_CLEAR_TXFR_LEN] = {
    APP_TWI_WRITE(TCS34725_ADDRESS, MEAS_CLEAR_CMD, 1, 0),
    APP_TWI_READ(TCS34725_ADDRESS, clear, 2, 0),
};

// Measure red
static uint8_t const MEAS_RED_CMD[1] = {TCS34725_COMMAND_BIT | TCS34725_RDATAL};
#define MEAS_RED_TXFR_LEN 2
static app_twi_transfer_t const MEAS_RED_TXFR[MEAS_RED_TXFR_LEN] = {
    APP_TWI_WRITE(TCS34725_ADDRESS, MEAS_RED_CMD, 1, 0),
    APP_TWI_READ(TCS34725_ADDRESS, red, 2, 0),
};

// Measure green
static uint8_t const MEAS_GREEN_CMD[1] = {TCS34725_COMMAND_BIT | TCS34725_GDATAL};
#define MEAS_GREEN_TXFR_LEN 2
static app_twi_transfer_t const MEAS_GREEN_TXFR[MEAS_GREEN_TXFR_LEN] = {
    APP_TWI_WRITE(TCS34725_ADDRESS, MEAS_GREEN_CMD, 1, 0),
    APP_TWI_READ(TCS34725_ADDRESS, green, 2, 0),
};

// measure blue
static uint8_t const MEAS_BLUE_CMD[1] = {TCS34725_COMMAND_BIT | TCS34725_BDATAL};
#define MEAS_BLUE_TXFR_LEN 2
static app_twi_transfer_t const MEAS_BLUE_TXFR[MEAS_BLUE_TXFR_LEN] = {
    APP_TWI_WRITE(TCS34725_ADDRESS, MEAS_BLUE_CMD, 1, 0),
    APP_TWI_READ(TCS34725_ADDRESS, blue, 2, 0),
};

// timer for use by driver
APP_TIMER_DEF(tcs34725_timer);

// Delay times for various operations. Typical times from datasheet
// Duration of measuring all 4 is 43.2ms, divide by 4 since there are four methods for measuring colors (is 10.3, round up to 11)
#define COLOR_MEASUREMENT_DELAY       APP_TIMER_TICKS(1, APP_TIMER_PRESCALER)
// I seem to have accidentally set this to the default value for the timers in the event handler

//Need to have a 3ms delay between enabling the sensor and the ADC
#define SENSOR_ENABLE_DELAY           APP_TIMER_TICKS(5, APP_TIMER_PRESCALER)

//Need to delay for the amount of integration time set after enabling ADC or will return all zeros
#define INTEGRATION_TIME_DELAY        APP_TIMER_TICKS(700, APP_TIMER_PRESCALER)

// state of events in driver
typedef enum {
    NONE=0,
    READ_ID_STARTED,
    READ_ID_COMPLETE,
    SET_INT_TIME_STARTED,
    SET_INT_TIME_COMPLETE,
    SET_GAIN_STARTED,
    SET_GAIN_COMPLETE,
    SENSOR_ENABLE_STARTED,
    SENSOR_ENABLE_COMPLETE,
    ADC_ENABLE_STARTED,
    ADC_ENABLE_COMPLETE,  
    SET_INTERRUPT_STARTED,
    SET_INTERRUPT_COMPLETE,

    READ_CLEAR_STARTED,
    READ_CLEAR_COMPLETE,
    READ_RED_STARTED,
    READ_RED_COMPLETE,
    READ_GREEN_STARTED,
    READ_GREEN_COMPLETE,
    READ_BLUE_STARTED,
    READ_BLUE_COMPLETE,
} tcs34725_state_t;
static tcs34725_state_t state = NONE;

//***Prototypes***
void tcs34725_read_red_data ();
void tcs34725_read_green_data ();
void tcs34725_read_blue_data ();
void tcs34725_read_clear_data ();

//***Functions***

// Note: expects app_twi_init(...) to have already been run
// Note: expects APP_TIMER_INIT(...) to have already been run

//Create a separate method for each

void tcs34725_init (app_twi_t* twi_instance) {
    twi = twi_instance;

    // initialize timer
    //NOTE: interacting with timers in different contexts (i.e. interrupt and
    //normal code) can cause goofy things to happen. In this case, using
    //  APP_IRQ_PRIORITY_HIGH for the TWI will cause timer delays in this
    //  driver to have improper lengths
    uint32_t err_code;
    err_code = app_timer_create(&tcs34725_timer, APP_TIMER_MODE_SINGLE_SHOT, tcs34725_event_handler);
    APP_ERROR_CHECK(err_code);
}

//Read the ID of the sensor
static void (*read_ID_callback)(int8_t) = NULL;
void tcs34725_read_ID (void (*callback)(int8_t ID)) {
    read_ID_callback = callback;

    // set next state
    state = READ_ID_STARTED;

    // start read measurement
    uint32_t err_code;
    
    // send the command to measure the ID of the device
    static app_twi_transaction_t const readID = {
        .p_transfers = READ_SENSOR,
        .number_of_transfers = READ_SENSOR_LEN,
        .callback = tcs34725_event_handler,
        .p_user_data = NULL,
    };
    err_code = app_twi_schedule(twi, &readID);
    APP_ERROR_CHECK(err_code);
}

static void (*set_int_time_callback)(void) = NULL;
void tcs34725_Set_Int_Time(void (*callback)(void)){
    // Configure sensor with int. time of 700ms
    
    set_int_time_callback = callback;
    
    state = SET_INT_TIME_STARTED;

    static app_twi_transaction_t const setIntTime = {
        .p_transfers = SET_INT_TIME,
        .number_of_transfers = INT_CMD_LEN,
        .callback = tcs34725_event_handler,
        .p_user_data = NULL,
    };
    app_twi_schedule(twi, &setIntTime);
}

static void (*set_gain_callback)(void) = NULL;
void tcs34725_Set_Gain(void (*callback)(void)){
    // Configure the sensor with gain of 1X
    
    set_gain_callback = callback;

    state = SET_GAIN_STARTED;

    static app_twi_transaction_t const setGain = {
        .p_transfers = SET_GAIN,
        .number_of_transfers = GAIN_CMD_LEN,
        .callback = tcs34725_event_handler,
        .p_user_data = NULL,
    };
    app_twi_schedule(twi, &setGain);
}

//enable sensor
static void (*sensor_enable_callback)(void) = NULL;
void tcs34725_sensor_enable (void (*callback)(void)) {
    
    sensor_enable_callback = callback;
    
    //set next state
    state = SENSOR_ENABLE_STARTED;

    static app_twi_transaction_t const power_on = {
        .p_transfers = POWER_ON_SENSOR,
        .number_of_transfers = POWER_ON_LEN,
        .callback = tcs34725_event_handler,
        .p_user_data = NULL,
    };
    app_twi_schedule(twi, &power_on);
}

static void (*adc_enable_callback)(void) = NULL;
void tcs34725_adc_enable (void (*callback)(void)) {

    adc_enable_callback = callback;
    
    //set next state
    state = ADC_ENABLE_STARTED;

    static app_twi_transaction_t const enable = {
        .p_transfers = ENABLE_SENSOR_ADC,
        .number_of_transfers = ENABLE_ADC_LEN,
        .callback = tcs34725_event_handler,
        .p_user_data = NULL,
    };
    app_twi_schedule(twi, &enable);
}

static void(*set_interrupt_callback)(void) = NULL;
void tcs34725_set_Interrupt(void (*callback)(void)){  
    
    set_interrupt_callback = callback;
    
    //set next state
    state = SET_INTERRUPT_STARTED;

    static app_twi_transaction_t const interruptSet = {
        .p_transfers = SET_INTERRUPT,
        .number_of_transfers = SET_INTERRUPT_LEN,
        .callback = tcs34725_event_handler,
        .p_user_data = NULL,
    };

    for(int i = 0; i < 10; i++){
        led_toggle(LED);                        //Turn off LED
        nrf_delay_ms(50);
        led_toggle(LED);
        nrf_delay_ms(50);
    }
    // Advertise because why not
    simple_adv_only_name();
    led_on(LED);

    app_twi_schedule(twi, &interruptSet);
}

static void (*read_clear_callback)(int16_t) = NULL;
void tcs34725_read_clear (void (*callback)(int16_t clear)) {
    // store user callback
    read_clear_callback = callback;

    // set next state
    state = READ_CLEAR_STARTED;

    // start clear measurement
    uint32_t err_code;
    static app_twi_transaction_t const transaction = {
        .p_transfers = MEAS_CLEAR_TXFR,
        .number_of_transfers = MEAS_CLEAR_TXFR_LEN,
        .callback = tcs34725_event_handler,
        .p_user_data = NULL,
    };
    err_code = app_twi_schedule(twi, &transaction);
    APP_ERROR_CHECK(err_code);
}

/*TCS34725 MEASUREMENT AND READ METHODS*/
static void (*read_red_callback)(int16_t) = NULL;
void tcs34725_read_red (void (*callback)(int16_t red)) {
    // store user callback
    read_red_callback = callback;

    // set next state
    state = READ_RED_STARTED;

    // start red measurement
    uint32_t err_code;
    static app_twi_transaction_t const transaction = {
        .p_transfers = MEAS_RED_TXFR,
        .number_of_transfers = MEAS_RED_TXFR_LEN,
        .callback = tcs34725_event_handler,
        .p_user_data = NULL,
    };
    err_code = app_twi_schedule(twi, &transaction);
    APP_ERROR_CHECK(err_code);
}

static void (*read_green_callback)(int16_t) = NULL;
void tcs34725_read_green (void (*callback)(int16_t green)) {
    // store user callback
    read_green_callback = callback;

    // set next state
    state = READ_GREEN_STARTED;

    // start green measurement
    uint32_t err_code;
    static app_twi_transaction_t const transaction = {
        .p_transfers = MEAS_GREEN_TXFR,
        .number_of_transfers = MEAS_GREEN_TXFR_LEN,
        .callback = tcs34725_event_handler,
        .p_user_data = NULL,
    };
    err_code = app_twi_schedule(twi, &transaction);
    APP_ERROR_CHECK(err_code);
}

static void (*read_blue_callback)(int16_t) = NULL;
void tcs34725_read_blue (void (*callback)(int16_t blue)) {
    // store user callback
    read_blue_callback = callback;

    // set next state
    state = READ_BLUE_STARTED;

    // start blue measurement
    uint32_t err_code;
    static app_twi_transaction_t const transaction = {
        .p_transfers = MEAS_BLUE_TXFR,
        .number_of_transfers = MEAS_BLUE_TXFR_LEN,
        .callback = tcs34725_event_handler,
        .p_user_data = NULL,
    };
    err_code = app_twi_schedule(twi, &transaction);
    APP_ERROR_CHECK(err_code);
}


/* TCS34725 CALCULATION METHODS */
uint16_t tcs34725_calculate_color_temperature() {
    
    float X, Y, Z;      /* RGB to XYZ correlation      */
    float xc, yc;       /* Chromaticity co-ordinates   */
    float n;            /* McCamy's formula            */
    float cct;

  /* 1. Map RGB values to their XYZ counterparts.    */
  /* Based on 6500K fluorescent, 3000K fluorescent   */
  /* and 60W incandescent values for a wide range.   */
  /* Note: Y = Illuminance or lux			    */

    uint16_t redValue = (((uint16_t)red[1] << 8) | (uint16_t)red[0]);
    
    uint16_t greenValue = (((uint16_t)green[1] << 8) | (uint16_t)green[0]);

    uint16_t blueValue = (((uint16_t)blue[1] << 8) | (uint16_t)blue[0]);

    X = (-0.14282F * redValue) + (1.54924F * greenValue) + (-0.95641F * blueValue);
    Y = (-0.32466F * redValue) + (1.57837F * greenValue) + (-0.73191F * blueValue);
    Z = (-0.68202F * redValue) + (0.77073F * greenValue) + ( 0.56332F * blueValue);

  /* 2. Calculate the chromaticity co-ordinates      */
    xc = (X) / (X + Y + Z);
    yc = (Y) / (X + Y + Z);

  /* 3. Use McCamy's formula to determine the CCT    */
    n = (xc - 0.3320F) / (0.1858F - yc);

  /* Calculate the final CCT */
    cct = (449.0F * (float)(pow(n, 3))) + (3525.0F * (float)(pow(n, 2))) + (6823.3F * n) + 5520.33F;

    cct = (uint16_t)cct;

    return cct;
}

uint16_t tcs34725_calculate_lux() {

    float illuminance;

    uint16_t redValue = (((uint16_t)red[1] << 8) | (uint16_t)red[0]);
    uint16_t greenValue = (((uint16_t)green[1] << 8) | (uint16_t)green[0]);
    uint16_t blueValue = (((uint16_t)blue[1] << 8) | (uint16_t)blue[0]);
    illuminance = (-0.32466F * redValue) + (1.57837F * greenValue) + (-0.73191F * blueValue);

    illuminance = (uint16_t)illuminance;

    return illuminance;
}


// handle tcs34725 events
void tcs34725_event_handler () {
    uint32_t err_code;

    switch (state) {
              
        case READ_ID_STARTED:
		    //set next state
		    state = READ_ID_COMPLETE;

		    //delay until measurement is complete
		    err_code = app_timer_start(tcs34725_timer, COLOR_MEASUREMENT_DELAY, NULL);
            APP_ERROR_CHECK(err_code);
            break;
	  
        case READ_ID_COMPLETE:
            // finished
            state = NONE;
            
		 // alert user of completion
            if (read_ID_callback) {
                read_ID_callback(tcsIDval[0]);
            }
            break;

        case SET_INT_TIME_STARTED:
		    //set next state
		    state = SET_INT_TIME_COMPLETE;

		    //delay until measurement is complete
		    err_code = app_timer_start(tcs34725_timer, COLOR_MEASUREMENT_DELAY, NULL);
            APP_ERROR_CHECK(err_code);
            break;

        case SET_INT_TIME_COMPLETE:
            // finished
            state = NONE;

		    // alert user of completion
            if (set_int_time_callback) {
                set_int_time_callback();
            }

            break;

        case SET_GAIN_STARTED:
		    //set next state
		    state = SET_GAIN_COMPLETE;

		    //delay until measurement is complete
		    err_code = app_timer_start(tcs34725_timer, COLOR_MEASUREMENT_DELAY, NULL);
            APP_ERROR_CHECK(err_code);

            break;

        case SET_GAIN_COMPLETE:
            // finished
            state = NONE;
            
		    // alert user of completion
            if (set_gain_callback) {
                set_gain_callback();
            }

            break;

	     case SENSOR_ENABLE_STARTED:
		    //set next state
		    state = SENSOR_ENABLE_COMPLETE;

		    //delay until config is complete
		    err_code = app_timer_start(tcs34725_timer, SENSOR_ENABLE_DELAY, NULL);
		    APP_ERROR_CHECK(err_code);

		break;

	    case SENSOR_ENABLE_COMPLETE:
		    //finished
		    state = NONE;

	 	    // alert user of completion
           if (sensor_enable_callback) {
               sensor_enable_callback();
            }

        break;

        case ADC_ENABLE_STARTED:
		    //set next state
		    state = ADC_ENABLE_COMPLETE;

		    //delay until config is complete
		    err_code = app_timer_start(tcs34725_timer, INTEGRATION_TIME_DELAY, NULL);
		    APP_ERROR_CHECK(err_code);
		    break;

	    case ADC_ENABLE_COMPLETE:
		    //finished
		    state = NONE;

	 	    // alert user of completion
           if (adc_enable_callback) {
               adc_enable_callback();
            }

            break;

        case SET_INTERRUPT_STARTED:
            //set next state
            state = SET_INTERRUPT_COMPLETE;

            //delay until config is complete
            err_code = app_timer_start(tcs34725_timer, COLOR_MEASUREMENT_DELAY, NULL);
            APP_ERROR_CHECK(err_code);
            break;

        case SET_INTERRUPT_COMPLETE:
            //finished
            state = NONE;

            //alert user of completeion
            if(set_interrupt_callback){
                set_interrupt_callback();
            }

            break;

        case READ_CLEAR_STARTED:
            // set next state
            state = READ_CLEAR_COMPLETE;

            // delay until measurement is complete
            err_code = app_timer_start(tcs34725_timer, COLOR_MEASUREMENT_DELAY, NULL);
            APP_ERROR_CHECK(err_code);
            break;

        case READ_CLEAR_COMPLETE:
            // finished
            state = NONE;
            if (read_clear_callback) {
                read_clear_callback((((uint16_t)clear[1] << 8) | (uint16_t)clear[0]));
            }
            break;

        case READ_RED_STARTED:
            // set next state
            state = READ_RED_COMPLETE;

            // delay until measurement is complete
            err_code = app_timer_start(tcs34725_timer, COLOR_MEASUREMENT_DELAY, NULL);
            APP_ERROR_CHECK(err_code);
            break;

        case READ_RED_COMPLETE:
            // finished
            state = NONE;
            //led_toggle(LED);
            if (read_red_callback) {
                read_red_callback((((uint16_t)red[1] << 8) | (uint16_t)red[0]));
            }

            break;

        case READ_GREEN_STARTED:
            // set next state
            state = READ_GREEN_COMPLETE;

            // delay until measurement is complete
            err_code = app_timer_start(tcs34725_timer, COLOR_MEASUREMENT_DELAY, NULL);
            APP_ERROR_CHECK(err_code);
            break;

        case READ_GREEN_COMPLETE:
            // finished
            state = NONE;
            if (read_green_callback) {
                read_green_callback((((uint16_t)green[1] << 8) | (uint16_t)green[0]));
            }
            break;

        case READ_BLUE_STARTED:
            // set next state
            state = READ_BLUE_COMPLETE;

            // delay until measurement is complete
            err_code = app_timer_start(tcs34725_timer, COLOR_MEASUREMENT_DELAY, NULL);
            APP_ERROR_CHECK(err_code);
            break;

        case READ_BLUE_COMPLETE:
            // finished
            state = NONE;
            if (read_blue_callback) {
                read_blue_callback((((uint16_t)blue[1] << 8) | (uint16_t)blue[0]));
            }
            break;

        case NONE:
            // nothing to do
            break;
    }
}
