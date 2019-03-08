#include <stdint.h>
#include <stdbool.h>
#include "nordic_common.h"
#include "app_timer.h"

#define SIMPLE_TIMER_PRESCALER     0
#define SIMPLE_TIMER_OP_QUEUE_SIZE 4

// Define four timers that the app can use.
// This is a bit of a hack, but makes for a really simple API.
// If your application needs more, or better control, please use the
// Nordic API directly.
APP_TIMER_DEF(timer00);
APP_TIMER_DEF(timer01);
APP_TIMER_DEF(timer02);
APP_TIMER_DEF(timer03);

static uint8_t _in_use = 0;

// This only needs to be called if you are NOT calling simple_ble_init If you
//  are using simple_ble, calling this again will still work, but wastes ~500
//  bytes of RAM
void simple_timer_init () {
	APP_TIMER_INIT(SIMPLE_TIMER_PRESCALER,
                   SIMPLE_TIMER_OP_QUEUE_SIZE,
                   NULL);
}

uint32_t simple_timer_start (uint32_t milliseconds,
                             app_timer_timeout_handler_t callback) {
	const app_timer_id_t* timer;
	uint32_t err_code;

	// Make sure we have a timer left
	if (_in_use > 3) {
		return NRF_ERROR_NO_MEM;
	}

	if (_in_use == 0)      timer = &timer00;
	else if (_in_use == 1) timer = &timer01;
	else if (_in_use == 2) timer = &timer02;
	else if (_in_use == 3) timer = &timer03;

	// Mark a new timer in use
	_in_use++;

	// Create and init the timer
	err_code = app_timer_create(timer, APP_TIMER_MODE_REPEATED, callback);
	if (err_code != NRF_SUCCESS) return err_code;

	// Actually start the timer
	err_code = app_timer_start(*timer,
	                           APP_TIMER_TICKS(milliseconds, SIMPLE_TIMER_PRESCALER),
	                           NULL);
	return err_code;
}
