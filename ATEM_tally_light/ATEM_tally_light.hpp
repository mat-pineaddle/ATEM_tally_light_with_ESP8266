/*
    Copyright (C) 2023 Aron N. Het Lam, aronhetlam@gmail.com

    This program makes an ESP8266 into a wireless tally light system for ATEM switchers,
    by using Kasper Skårhøj's (<https://skaarhoj.com>) ATEM client libraries for Arduino.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "Arduino.h"
#include "ImprovWiFiLibrary.h"

//Perform initial setup on power on
//Handle the change of states in the program
void changeState(uint8_t stateToChangeTo);

//Set the color of both LEDs
void setBothLEDs(uint8_t color);

//Set the color of the 1st LED
void setLED1(uint8_t color);

//Set the color of the 2nd LED
void setLED2(uint8_t color);

//Set the color of a LED using the given pins
void setLED(uint8_t color, int pinRed, int pinGreen, int pinBlue);

void analogWriteWrapper(uint8_t pin, uint8_t value);

//True for colors that can be used as idle color
bool isValidIdleColor(uint8_t color);

//Write one LED color channel (0 = off, 255 = on, else PWM)
void writeLEDChannel(uint8_t pin, uint8_t value);

//Set the color of the LED strip, except for the status LED
void setSTRIP(uint8_t color);

//Set the single status LED (last LED)
void setStatusLED(uint8_t color);

#ifdef DEBUG_LED_STRIP
void printLeds();
#endif

//True if the selected switcher is a RODECaster Video
bool isRcv();

#ifndef TALLY_TEST_SERVER
//True if connected to the selected switcher
bool switcherConnected();

//Inputs per RODECaster Video scene (automatic layout + manual selection)
const uint8_t *rcvSceneMasks();

//Print RODECaster Video program/preview state to serial
void printRcvState();
#endif

int getTallyState(uint16_t tallyNo);

int getLedColor(int tallyMode, int tallyNo);

//Serve setup web page to client, by sending HTML with the correct variables
void handleRoot();

//Save new settings from client in EEPROM and restart the ESP8266 module
void handleSave();

//Send 404 to client in case of invalid webpage being requested.
void handleNotFound();

String getSSID();

void setWiFi(String ssid, String pwd);

// void improvCallback(improv::ImprovCommand d);
//Commented out for users without batteries - Also timer is not done properly
//Main loop for things that should work every second
// void batteryLoop();
