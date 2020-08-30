#include <dummy.h>

#include <algorithm>
#include <iostream>
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <Button2.h>
#include <Wire.h>
#include <BH1750.h>          //Helligkeit
#include <DHT12.h>           //Temperatur
#include <Adafruit_BME280.h> //Barrometer
#include <WiFiMulti.h>
#include "esp_wifi.h"

#define SOFTAP_MODE
//#define USE_18B20_TEMP_SENSOR
#define I2C_SDA             25
#define I2C_SCL             26
#define DHT12_PIN           16
#define BAT_ADC             33
#define SALT_PIN            34
#define SOIL_PIN            32
#define BOOT_PIN            0
#define POWER_CTRL          4
#define USER_BUTTON         35
#define DS18B20_PIN         21                  //18b20 data pin
#define uS_TO_S_FACTOR 1000000    /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  300        /* Time ESP32 will go to sleep (in seconds)300 */

RTC_DATA_ATTR int bootCount = 0;

BH1750 lightMeter(0x23);                         //0x23
Adafruit_BME280 bmp;                             //0x77 0xff
DHT12 dht12(DHT12_PIN, true);
Button2 button(BOOT_PIN);
Button2 useButton(USER_BUTTON);
WiFiMulti multi;
//DS18B20 temp18B20(DS18B20_PIN);

#define WIFI_SSID   "xxxxx"                  //Your own SSID
#define WIFI_PASSWD "xxxxx"                  //Your own password
bool bme_found = false;

WiFiClient client;
const char *host = "api.thingspeak.com";          //IP address of the thingspeak server
const char *api_key ="xxxxxx";                    // Your own thingspeak api_key
const int httpPort = 80;
long uploadTime = 0;
void uploadTemperatureHumidity();

class DS18B20                                     // Simple ds18b20 class
{
public:
    DS18B20(int gpio)
    {
        pin = gpio;
    }

    float temp()
    {
        uint8_t arr[2] = {0};
        if (reset()) {
            wByte(0xCC);
            wByte(0x44);
            delay(750);
            reset();
            wByte(0xCC);
            wByte(0xBE);
            arr[0] = rByte();
            arr[1] = rByte();
            reset();
            return (float)(arr[0] + (arr[1] * 256)) / 16;
        }
        return 0;
    }
private:
    int pin;

    void write(uint8_t bit)
    {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        delayMicroseconds(5);
        if (bit)digitalWrite(pin, HIGH);
        delayMicroseconds(80);
        digitalWrite(pin, HIGH);
    }

    uint8_t read()
    {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        delayMicroseconds(2);
        digitalWrite(pin, HIGH);
        delayMicroseconds(15);
        pinMode(pin, INPUT);
        return digitalRead(pin);
    }

    void wByte(uint8_t bytes)
    {
        for (int i = 0; i < 8; ++i) {
            write((bytes >> i) & 1);
        }
        delayMicroseconds(100);
    }

    uint8_t rByte()
    {
        uint8_t r = 0;
        for (int i = 0; i < 8; ++i) {
            if (read()) r |= 1 << i;
            delayMicroseconds(15);
        }
        return r;
    }

    bool reset()
    {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        delayMicroseconds(500);
        digitalWrite(pin, HIGH);
        pinMode(pin, INPUT);
        delayMicroseconds(500);
        return digitalRead(pin);
    }
};


void smartConfigStart(Button2 &b)
{
    Serial.println(" Knopf gedrückt auf der rückseite...");

}

void sleepHandler(Button2 &b)
{
    Serial.println("Enter Deepsleep ...");
    esp_sleep_enable_ext1_wakeup(GPIO_SEL_35, ESP_EXT1_WAKEUP_ALL_LOW);
    delay(1000);
    esp_deep_sleep_start();
}

void setup()
{
  Serial.begin(115200);
  delay(10);
  setup_connect_to_WiFi(); //Call for the WiFi Connection

    button.setLongClickHandler(smartConfigStart);
    useButton.setLongClickHandler(sleepHandler);

    Wire.begin(I2C_SDA, I2C_SCL);
    //Auslesen des Luxsensors starten
    lightMeter.begin();
    dht12.begin();

    //! Sensor power control pin , use deteced must set high
    pinMode(POWER_CTRL, OUTPUT);
    digitalWrite(POWER_CTRL, 1);
    delay(1000);

    if (bmp.begin()) {
        Serial.println(F("Could not find a valid BMP280 sensor, check wiring!"));
        bme_found = false;
    } else {
        Serial.println(F("BMP280 sensor, check OK!"));
        bme_found = true;
    }

    if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
        Serial.println(F("BH1750 Advanced begin"));
    } else {
        Serial.println(F("Error initialising BH1750"));
    }

}

void setup_connect_to_WiFi(){
  unsigned int WiFi_connect_delay_try = 500; //Delay of the retry to connect to WiFi
  WiFi.setHostname("Ausensensor");
  WiFi.mode(WIFI_STA); //Indicate to act as wifi_client only, defaults to act as both a wifi_client and an access-point.
  WiFi.begin(WIFI_SSID, WIFI_PASSWD); //Try to connect now
  Serial.print("Connecting to \"" + String(WIFI_SSID) + "\" with " + String(WiFi_connect_delay_try) + "ms interval with mode WIFI_STA"); Serial.printf("%s", WiFi.mode(WIFI_STA) ? "" : ", mode failed!!");
  while(WiFi.status() != WL_CONNECTED){
    /*Note: if connection is established, and then lost for some reason, ESP will automatically reconnect. This will be done automatically by Wi-Fi library, without any user intervention.*/
    delay(WiFi_connect_delay_try);
    Serial.print(".");
  }
  Serial.print("\nConnected to " + String(WIFI_SSID) + " with IP Address: "); Serial.print(WiFi.localIP()); Serial.print("\n");
}

uint32_t readSalt()
{
    uint8_t samples = 120;
    uint32_t humi = 0;
    uint16_t array[120];

    for (int i = 0; i < samples; i++) {
        array[i] = analogRead(SALT_PIN);
        delay(2);
    }
    std::sort(array, array + samples);
    for (int i = 0; i < samples; i++) {
        if (i == 0 || i == samples - 1)continue;
        humi += array[i];
    }
    humi /= samples - 2;
    return humi;
}

uint16_t readSoil()
{
    uint16_t soil = analogRead(SOIL_PIN);
    return map(soil, 0, 4095, 100, 0);
}

float readBattery()
{
    int vref = 1100;
    uint16_t volt = analogRead(BAT_ADC);
    float battery_voltage = ((float)volt / 4095.0) * 2.0 * 3.3 * (vref);
    return battery_voltage;
}

void loop()
{
 

    button.loop();
    useButton.loop();    
//     setup_connect_to_WiFi(); //Call for the WiFi Connection
   if(!client.connect(host, httpPort)){
   Serial.println("kein WLAN :-(");
   return;
  }
        delay(2000); //zeitverzögerung BME280 und BH1750
            float lux = lightMeter.readLightLevel();
           Serial.print("Helligkeit: ");                       //feld5
           Serial.println(lux);

            float bme_temp = bmp.readTemperature();
            float bme_pressure = bmp.readPressure() / 100.0F;  //feld 8
            float bme_altitude = bmp.readAltitude(1013.25);    //feld 7
           Serial.print("BME Temperatur: ");
           Serial.println(bme_temp);
           Serial.print("BME Luftdruck: ");
           Serial.println(bme_pressure);
           Serial.print("BME Luftdruckhöhe: ");
           Serial.println(bme_altitude);
            float t12 = dht12.readTemperature();
            float h12 = dht12.readHumidity();            
           Serial.print("Temperatur: ");                       //feld 1
           Serial.println(t12);    
           Serial.print("Feuchtigkeit: ");                     //feld 4
           Serial.println(h12);
            uint16_t soil = readSoil();
            uint32_t salt = readSalt();
            float bat = readBattery();                         //feld3
           Serial.print("Bodenfeuchte: ");
           Serial.println(soil);
           Serial.print("Bodensalzgehalt: ");                  //feld2
           Serial.println(salt);
           Serial.print("Batterie Spannung: ");                //feld6
           Serial.println(bat);

          Serial.println("Senden an Thingspeak");
                                                               // Three values(field1 field2 field3 field4 field5 field6 field7 field8) have been set in thingspeak.com 
  client.print(String("GET ") + "/update?api_key="+api_key+"&field1="+t12+"&field2="+salt+ "&field3="+soil+"&field4="+h12+"&field5="+lux+"&field6="+bat+"&field7="+bme_altitude+"&field8="+bme_pressure+" HTTP/1.1\r\n" +"Host: " + host + "\r\n" + "Connection: close\r\n\r\n");
  while(client.available()){
    String line = client.readStringUntil('\r');
    Serial.print(line);
    
  }
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
#ifdef USE_18B20_TEMP_SENSOR
            //Single data stream upload
            float temp = temp18B20.temp();
           Serial.println("Temperatur3");         //feld 1 vieleicht
           Serial.println(temp);    
#endif
        Serial.println("Enter Deepsleep ...");
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
delay(1000); //zeitverzögerung für bm3280
  esp_deep_sleep_start();   
        Serial.println("wacke up Deepsleep ...");
}
