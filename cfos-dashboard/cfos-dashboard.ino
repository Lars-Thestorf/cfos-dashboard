
// Example sketch which shows how to display some patterns
// on a 64x32 LED matrix
//

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>

#include "icons.h"

const char* ssid = "MY_SSID";
const char* password = "MY_PASSWORD";
const char* serverName = "http://CHARGING.MANAGER.IP/cnf?cmd=get_dev_info";

#define REFRESH_INTERVALL 5000

#define R1_PIN 4
#define G1_PIN 16
#define B1_PIN 12
#define R2_PIN 2
#define G2_PIN 17
#define B2_PIN 15
#define A_PIN 23
#define B_PIN 5
#define C_PIN 22
#define D_PIN 18
#define E_PIN -1 // required for 1/32 scan panels, like 64x64px. Any available pin would do, i.e. IO32
#define LAT_PIN 19
#define OE_PIN 21
#define CLK_PIN 14

HUB75_I2S_CFG::i2s_pins _pins={R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN};

#define PANEL_RES_X 64      // Number of pixels wide of each INDIVIDUAL panel module. 
#define PANEL_RES_Y 32     // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1      // Total number of panels chained one to another
 
//MatrixPanel_I2S_DMA dma_display;
MatrixPanel_I2S_DMA *dma_display = nullptr;

uint16_t myBLACK = dma_display->color565(0, 0, 0);
uint16_t myWHITE = dma_display->color565(255, 255, 255);
uint16_t myRED = dma_display->color565(255, 0, 0);
uint16_t myGREEN = dma_display->color565(0, 255, 0);
uint16_t myBLUE = dma_display->color565(0, 0, 255);


void setup() {
  Serial.begin(115200);

  // Module configuration
  HUB75_I2S_CFG mxconfig(
    PANEL_RES_X,   // module width
    PANEL_RES_Y,   // module height
    PANEL_CHAIN,    // Chain length
    _pins
  );

  //mxconfig.gpio.e = 18;
  //mxconfig.clkphase = false;
  //mxconfig.driver = HUB75_I2S_CFG::FM6126A;

  // Display Setup
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(90); //0-255
  dma_display->clearScreen();
  dma_display->setTextColor(0x8410);
  dma_display->setCursor(0, 0);
  dma_display->write("Connecting");

  WiFi.begin(ssid, password);
  Serial.println("Connecting");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());
  dma_display->clearScreen();
  dma_display->setCursor(0, 0);
  dma_display->write("Connected");
}

String httpGETRequest(const char* serverName) {
  WiFiClient client;
  HTTPClient http;
    
  // Your Domain name with URL path or IP address with path
  http.begin(client, serverName);
  
  // Send HTTP POST request
  int httpResponseCode = http.GET();
  
  String payload = "{}"; 
  
  if (httpResponseCode>0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();

  return payload;
}

int get_color(int value, int highest) {
  //lightest color: 0x2104 => R=00100 G=001000 B=00100
  int green = map(abs(value), 0, highest, 0b001000, 0b111111);
  int red = green >> 1;
  int blue = green >> 1;
  return (red << 11) | (green << 5) | blue;
}

void loop() {
  if(WiFi.status()== WL_CONNECTED){
    String sensorReadings;
    sensorReadings = httpGETRequest(serverName);
    //Serial.println(sensorReadings);
    JSONVar myObject = JSON.parse(sensorReadings);

    // JSON.typeof(jsonVar) can be used to get the type of the var
    if (JSON.typeof(myObject) == "undefined") {
      Serial.println("Parsing input failed!");
      return;
    }
  
    Serial.print("JSON object = ");
    Serial.println(myObject);

    if (myObject.hasOwnProperty("devices")) {
      JSONVar dev_array = myObject["devices"];
      int length = dev_array.length();
      Serial.println(length);

      bool got_consumption = false;
      int consumption = 0;
      bool got_generation = false;
      int generation = 0;
      bool got_vehicle = false;
      int vehicle = 0;
      bool got_grid = false;
      int grid = 0;

      for (int i=0; i < length; i++) {
        JSONVar item = dev_array[i];

        if (!item.hasOwnProperty("is_evse")) {
          continue;
        }
        if (item["is_evse"]) {
          continue;
        }            
        if (!item.hasOwnProperty("role") || !item.hasOwnProperty("power_w")) {
          continue;
        }
        
        int value = item["power_w"];
        int role = item["role"];
        switch(role) {
          case 1:
            got_consumption = true;
            consumption += value;
            break;
          case 2:
            got_generation = true;
            generation += value;
            break;
          case 3:
            got_grid = true;
            grid += value;
            break;
          case 4:
            got_vehicle = true;
            vehicle += value;
            break;
        }
      }

      //calculate missing values
      if (got_grid && !got_consumption && !got_generation && got_vehicle) {
        if (grid > 0) {
          consumption = grid - vehicle;
        } else {
          generation = - grid + vehicle;
        }
      }
      if (got_grid && !got_consumption && got_generation && got_vehicle) {
        consumption = grid + generation - vehicle;
      }
      if (got_grid && got_consumption && !got_generation && got_vehicle) {
        generation = - grid + consumption + vehicle;
      }
      if (got_grid && got_consumption && got_generation && !got_vehicle) {
        vehicle = grid - consumption + generation;
      }
      if (!got_grid && got_consumption && got_generation && got_vehicle) {
        grid = consumption - generation + vehicle;
      }

      //arrow color
      int highest = max(max(grid, generation), max(consumption, vehicle));

      dma_display->clearScreen();
      dma_display->drawIcon(grid_icon, 2,2,8,12);
      dma_display->drawIcon(solar_icon, 1,18,10,12);
      dma_display->drawIcon(home_icon, 53,2,10,12);
      dma_display->drawIcon(car_icon, 53,20,10,10);

      float power_kw = abs(grid / 1000.0);
      char tmp[5];
      snprintf(tmp, 5, "%.1f", power_kw);
      dma_display->setCursor(12, 1);
      dma_display->write(tmp);
      int color;
      color = get_color(grid, highest);
      if (grid != 0) {
        dma_display->drawLine(25, 9, 31, 15, color);
      }
      if (grid < 0) {
        dma_display->drawFastHLine(26, 9, 5, color);
        dma_display->drawFastVLine(25, 10, 5, color);
      }
      
      power_kw = abs(generation / 1000.0);
      snprintf(tmp, 5, "%.1f", power_kw);
      dma_display->setCursor(12, 24);
      dma_display->write(tmp);
      color = get_color(generation, highest);
      if (generation != 0) {
        dma_display->drawLine(25, 22, 31, 16, color);
      }
      if (generation < 0) {
        dma_display->drawFastHLine(26, 22, 5, color);
        dma_display->drawFastVLine(25, 17, 5, color);
      }

      power_kw = abs(consumption / 1000.0);
      snprintf(tmp, 5, "%.1f", power_kw);
      dma_display->setCursor(35, 1);
      dma_display->write(tmp);
      color = get_color(consumption, highest);
      if (consumption != 0) {
        dma_display->drawLine(32, 15, 38, 9, color);
      }
      if (consumption > 0) {
        dma_display->drawFastHLine(33, 9, 5, color);
        dma_display->drawFastVLine(38, 10, 5, color);
      }

      power_kw = abs(vehicle / 1000.0);
      snprintf(tmp, 5, "%.1f", power_kw);
      dma_display->setCursor(35, 24);
      dma_display->write(tmp);
      color = get_color(vehicle, highest);
      if (vehicle != 0) {
        dma_display->drawLine(32, 16, 38, 22, color);
      }          
      if (vehicle > 0) {
        dma_display->drawFastHLine(33, 22, 5, color);
        dma_display->drawFastVLine(38, 17, 5, color);
      }
    } else {
      Serial.print("devices array missing");        
      dma_display->clearScreen();
      dma_display->setCursor(0, 0);
      dma_display->write("Error");
    }
  }
  else {
    Serial.println("WiFi Disconnected");
    dma_display->clearScreen();
    dma_display->setCursor(0, 0);
    dma_display->write("Connecting");
  }
  delay(REFRESH_INTERVALL);
}
