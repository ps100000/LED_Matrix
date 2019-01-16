#include <dummy.h>

/*
IOT Actor Board Webserver Demo with configuration webpage
for ESP32 DevKitC
Elektor / Jens Nickel
My Journey in the IoT
*/

#include <limits.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <soc/gpio_struct.h>
#include <Preferences.h>   // this library is used to get access to Non-volatile storage (NVS) of ESP32
// see https://github.com/espressif/arduino-esp32/blob/master/libraries/Preferences/examples/StartCounter/StartCounter.ino

#include "Server.hpp"
#include "Wifi.hpp"

Preferences preferences;   // we must generate this object of the preference library

unsigned long matrix[8][8];
unsigned long pwm_count;
const gpio_num_t column_pins[8] = {GPIO_NUM_32 ,GPIO_NUM_33,GPIO_NUM_25,GPIO_NUM_26,GPIO_NUM_27,GPIO_NUM_14,GPIO_NUM_12,GPIO_NUM_13};
const gpio_num_t row_pins[8] = {GPIO_NUM_23,GPIO_NUM_22,GPIO_NUM_21,GPIO_NUM_19,GPIO_NUM_18,GPIO_NUM_5,GPIO_NUM_4,GPIO_NUM_15};

// this section handles configuration values which can be configured via webpage form in a webbrowser

// 8 configuration values max
String ConfigValue[8];    // the value itself (String)


// Initalize the values 
void InitializeConfigValues()
{
  for (int count = 0; count < 8; count++)
  {
    ConfigValue[count] = "";
  }
}

// Decodes a GET parameter (expression after ? in URI (URI = expression entered in address field of webbrowser)), like "Country=Germany&City=Aachen"
// and set the ConfigValues
void decode_url_param(String param)
{
  if(param.indexOf("field=", 0) > -1){
    int start = param.indexOf('field=', 1);       // search for =
    String field_param = param.substring(start + 1, param.length());  // extract everything between = and end of string
    if(field_param.length() >= 64){
      for(int i = 0; i < 8; i++){
        for(int j = 0; j < 8; j++){
          if(field_param[i * 8 + j] >= '0' && field_param[i * 8 + j] < '4'){
            if(field_param[i * 8 + j] == '3'){
              matrix[j][i] = ULONG_MAX - 1;
              Serial.print('M');
            }else{
              matrix[j][i] = (field_param[i * 8 + j] - '0') * 8;
              Serial.print(String(matrix[j][i]));
            }
          }
        }
        Serial.print("\n");
      }
    }
  }
}

String create_html(){
  String page = "<!DOCTYPE html><html><head><title>LED-Matrix</title><script>function field_clicked(e,c){field[e][c]=active_color;var o=\"\";switch(active_color){case 3:o=\"#000000\";break;case 2:o=\"#000040\";break;case 1:o=\"#000080\";break;case 0:o=\"#0000F0\"}document.getElementById(e+\"-\"+c).style.background=o}function select_color(e){active_color=e}function send(){for(var e=\"http://\"+window.location.hostname+\"/?field=\",o=0;8>o;o++)for(var c=0;8>c;c++)e+=field[c][o].toString(10);window.location.href=e}var field=[[3,3,3,3,3,3,3,3],[3,3,3,3,3,3,3,3],[3,3,3,3,3,3,3,3],[3,3,3,3,3,3,3,3],[3,3,3,3,3,3,3,3],[3,3,3,3,3,3,3,3],[3,3,3,3,3,3,3,3],[3,3,3,3,3,3,3,3]],active_color=0</script></head><body><table border=\"1\" style=\"border-style: solid; border-color: black; border-collapse: collapse; background-color: black\"><tr><td style=\"width: 32px; height: 32px; background-color: #000000\" onclick=\"select_color(3)\"></td><td style=\"width: 32px; height: 32px; background-color: #000040\" onclick=\"select_color(2)\"></td><td style=\"width: 32px; height: 32px; background-color: #000080\" onclick=\"select_color(1)\"></td><td style=\"width: 32px; height: 32px; background-color: #0000F0\" onclick=\"select_color(0)\"></td></tr></table><br><br><table border=\"1\" style=\"border-style: solid; border-color: black; border-collapse: collapse; background-color: black\">";
  for(int i = 0; i < 8; i++){
    page += "<tr>";
    for(int j = 0; j < 8; j++){
      page += "<td id=";
      page += j;
      page += "-";
      page += i;
      page += " style=\"width: 32px; height: 32px\" onclick=\"field_clicked(";
      page += j;
      page += ",";
      page += i;
      page += ")\"></td>";
    }
    page += "</tr>";
  }
  page += "</table><br><button style=\"background-color: forestgreen; width: 128px; height: 32px; border: none\" onclick=\"send()\">OKAY</button></body></html>";
  return page;
}

void setup() 
{
  for(int i = 0; i < 8; i++){
    pinMode(column_pins[i], OUTPUT);
    pinMode(row_pins[i], OUTPUT);
    digitalWrite(column_pins[i],LOW);
    digitalWrite(row_pins[i],LOW);
  }
  pinMode(2, OUTPUT);
  digitalWrite(2,HIGH);
  preferences.begin("Matrix", false);   // see https://github.com/espressif/arduino-esp32/blob/master/libraries/Preferences/examples/StartCounter/StartCounter.ino 
  WiFi_SetBothModesNetworkStationAndAccessPoint();
   
  // Open serial communications and wait for port to open:
  Serial.begin(115200);
  Serial.println("Hello ESP32!");

  // takeout 2 Strings out of the Non-volatile storage
  String strSSID = preferences.getString("SSID", "");
  String strPassword = preferences.getString("Password", "");

  // convert it to char*
  char* txtSSID = const_cast<char*>(strSSID.c_str());
  char* txtPassword = const_cast<char*>(strPassword.c_str());   // https://coderwall.com/p/zfmwsg/arduino-string-to-char 

   //try to connect to the LAN
  int success = WiFi_RouterNetworkConnect(txtSSID, txtPassword);
  if (success == 1){
      Serial.println("LAN connected!");
  }
  else{
      Serial.println("could not connect to LAN!");
  }   

  // Start access point with SSID "ESP32_MyJourneyIoT"
  int success2 = WiFi_AccessPointStart("LED-Matrix");
  if (success2 == 1){
      Serial.println("AcessPoint started!");
  }
  else{
      Serial.println("AcessPoint Error!");
  }
  // initialize config values and set first 3 names
  InitializeConfigValues();
 
  // start the webserver to listen for request of clients (in LAN or own ESP32 network)
  Webserver_Start();

  
  for(int i = 0; i < 8; i++){
    for(int j = 0; j < 8; j++){
      matrix[i][j] = ULONG_MAX - 1;
    }
  }
}

void loop() {

  while(1){
    String GETParameter = Webserver_GetRequestGETParameter();   // look for client request
    
    if (GETParameter.length() > 0)        // we got a request, client connection stays open
    {
        //TODO: config via serial
        
        String HTMLPageWithConfigForm;
        HTMLPageWithConfigForm = create_html();
        decode_url_param(GETParameter);
        Webserver_SendHTMLPage(HTMLPageWithConfigForm);    // send out the webpage to client = webbrowser and close client connection
    }

    if(Serial.available()){
      String command = Serial.readStringUntil('\n');
      if(command.startsWith("passwd:")){
        preferences.putString("Password",command.substring(7));
      }
      if(command.startsWith("SSID:")){
        preferences.putString("SSID",command.substring(5));
      }
      String strSSID = preferences.getString("SSID", "");
      String strPassword = preferences.getString("Password", "");
      Serial.print("SSID:");
      Serial.println(strSSID);
      Serial.print("Password:");
      Serial.println(strPassword.substring(0,4));
    }
    for(int i = 0; i < 8; i++){
      gpio_set_level(row_pins[i],1);
      for(int j = 0; j < 8; j++){
        if(0 == pwm_count % (matrix[j][i] + 1)){
          gpio_set_level(column_pins[j],1);
        }
      }
      delayMicroseconds(50);
      for(int j = 0; j < 8; j++){
        gpio_set_level(column_pins[j],0);
      }
      gpio_set_level(row_pins[i],0);
      
      delayMicroseconds(5);
    }
    for(int i = 0; i < 8; i++){
      GPIO.out_w1ts = (1 << row_pins[i]);
      for(int j = 7; j >= 0; j--){
        if(0 == pwm_count % (matrix[j][i] + 1)){
          gpio_set_level(column_pins[j],1);
        }
      }
      delayMicroseconds(50);
      for(int j = 0; j < 8; j++){
        gpio_set_level(column_pins[j],0);
      }
      gpio_set_level(row_pins[i],0);
      
      delayMicroseconds(5);
    }
    pwm_count++;
  }
}
