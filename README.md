# esp8266-temperature
Use an esp8266 with a 433Mhz RF receiver to connect and decode Acurite
thermometer settings, and post the results to MQTT

This is based on code created by Ray Wang at

  https://rayshobby.net/wordpress/reverse-engineer-wireless-temperature-humidity-rain-sensors-part-1/

I adopted it to an ESP8266.

You will need to make a file `network_conn.h` with contents similar to

    const char* ssid       = "your-ssid";
    const char* password   = "your-wpa-passwd";
    const char* mqttServer = "your-MQTT-server";
    const int mqttPort     = 1883;

in order to provide details of your network and MQTT server

The 433Mhz receiver is connected simply:

    433Mhz   ESP8266
    ======   =======
       VIN---3.3V
       GND---GND
      DATA---D2 (GPIO4)

If you change the DATA pin then you'll need to change the definition of
  #define DATAPIN  D2
This is attached to an interrupt.

The MQTT channels are based off the word "temp" and the last 6 digits of the MAC
   
     e.g  temp/123456/status

The format is JSON with "date", "degF" and "degC" entries
eg

  {"date":"Sun Dec  8 00:06:29 2019 GMT", "degF": "37.0", "degC": "2.8"}

Uses PubSubClient library on top of the ESP8266WiFi one
   
Original code by Ray Wang, April 2014
Ported to ESP8266 by Stephen Harris, Dec 2019
