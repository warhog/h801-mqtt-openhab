# h801-mqtt-openhab
MQTT firmware for h801 led driver specifically optimized for openHAB integration

this project is the implementation of a new firmware for the h801 led drivers which is specifically optimized for easy openHAB integration.
there is support for the HSB color model used by openHAB so that no RGB conversion is needed to be done in openHAB rules.

when there is a color change incoming the firmware is ramping to that color slowly depending on the speed setting.
also there is always a gamma correction done.

## MQTT topics
| topic | description |
|---|---|
| hsb | HSB color with hue, saturation and brightness. delimiter is the comma |
| white1 | brightness for white 1 output (0-100) |
| white2 | brightness for white 2 output (0-100) |
| speed | speed for fading to target colors (0-100, 0 = very slow, 100 = faster) |
| status | status (online or offline), during connection a LWT is set to offline |
| status/hsb | the current target color |
| status/white1 | the current target white 1 brightness |
| status/white2 | the current target white 2 brightness |
| status/speed | the current target speed |


# requirements
esp8266 support for arduino ide (https://github.com/esp8266/Arduino)
