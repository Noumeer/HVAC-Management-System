#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME280 bme;

#define CATOF_pin5 3
#define CATOF_pin4 4
#define CATOF_pin3 5
#define CATOF_pin2 6
#define CATOF_pin1 7
#define CATL_pin 2

#define CATL_redpin A0
#define CATL_bluepin A2
#define CATL_greenpin A1

float temp, hum, pres;
int CATOF,CATL;
String JSON_DB,CATL_str;
void setup()
{
//  Serial.begin(9600);
  Serial.begin(115200,SERIAL_8N1);
  bme.begin(0x76);
  for (int i = 2; i <= 7; i++)
  {
    pinMode(i, INPUT);
  }
  pinMode(CATL_redpin, OUTPUT);
  pinMode(CATL_greenpin, OUTPUT);
}
void loop()
{
  getDATA();
  uartloop();
  
}
void sendJSON()
{
  getDATA();
 
  JSON_DB += "{\r\n\"WaterLevel\": \"";
  JSON_DB += CATOF;
  JSON_DB += "\", \"Temp\": \"";
  JSON_DB += temp;
  JSON_DB += "\", \"RH\": \"";
  JSON_DB += hum;
  JSON_DB += "\", \"BMP\": \"";
  JSON_DB += pres;
  JSON_DB += "\", \"CATLock\": \"";
  JSON_DB += CATL_str;
  JSON_DB += "\"\r\n}";

  Serial.println(JSON_DB);

  JSON_DB = "";
}
void uartloop()
{
  
    sendJSON();
    delay(500);
    
}
void getDATA()
{
  temp = bme.readTemperature();
  pres = bme.readPressure() / 3386;
  hum = bme.readHumidity();
  CATL = !digitalRead(CATL_pin);
  
  if (CATL)
  {
    digitalWrite(CATL_greenpin, HIGH);
    digitalWrite(CATL_redpin, LOW);
    CATL_str = "true";
  }
  else
  {
    digitalWrite(CATL_redpin, HIGH);
    digitalWrite(CATL_greenpin, LOW);
    CATL_str = "false";
  }
  

  getLevel();
}
void getLevel()
{
int flag = 0; 

  if (digitalRead(CATOF_pin1))
  {
    flag = 1;
  }
  if (digitalRead(CATOF_pin2))
  {
    flag = 2;
  }
  if (digitalRead(CATOF_pin3))
  {
    flag = 3;
  }
  if (digitalRead(CATOF_pin4))
  {
    flag = 4;
  }
  if (digitalRead(CATOF_pin5))
  {
    flag = 5;
  }

  switch (flag)
  {
  case 1: CATOF = 0;
  break;
  case 2: CATOF = 25;
  break;
  case 3: CATOF = 50;
  break;
  case 4: CATOF = 75;
  break;
  case 5: CATOF = 100;
  break;
  default: CATOF = -1;
  }
}
