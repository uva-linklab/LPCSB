# LPCSB
LPCSB = Low Power Color Sensing Board

The LPCSB is designed to measure the ambient light in the surrounding area by means of a TCS34725 Color Sensor.  The sensor uses four analog-to-digital converters (ADCs) to read four photodiodes (one photodiode per ADC).  The photodiodes are red-filtered, green-filtered, blue-filtered, and clear (no filter).  The photodiodes are also covered with an IR-blocking filter.  The code in this repository is responsible for initializing all of the hardware on the LPCSB, configuring the settings of the color sensor (integration time, gain, enabling the internal oscillator, enabling the ADCs, and setting a hardware interrupt in the sensor), reading and saving the values taken by the four ADCs mentioned previously, and then calculating the the temperature of each color (how much of each is in the sample taken), as well as the lux (intensity per square meter) of said sample.  

All required parts to take data from the color sensor have been initialized and tested in this code, except for the BLE radio, which isn't working and is currently being troubleshooted.

Update: Got the BLE radio to work with code from "Squall", am currently working on code that will broadcast the captured color data.
NOTE:  In the future, make sure that the external crystal oscillator meets the BLE chip requirements for the external clock frequency described in the datasheet. In this case, the required frequency for the external clock is 16 MHz.  For some reason, the clock chips that were being used were 26 MHz clocks instead of 16 MHz, which WILL cause errors with whichever sections of the processor happen to rely on the external signal.
