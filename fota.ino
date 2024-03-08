#include <Arduino.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <SD.h>
#include <ESPmDNS.h>
#include <Arduino_JSON.h>
#include <HardwareSerial.h>
#include "user_app.h"
/**************************************************/
#define LED_BUILTIN   5 
#define BTN_FOTA   36 
#define SD_MISO     2       // SD-Card
#define SD_MOSI    15       // SD-Card maybe use an input only pin for mosi to free an in/out pin
#define SD_SCLK    14       // SD-Card
#define SD_CS      12       // SD-Card
#define TX_PIN_IAP 4        // uart tx to iap 
#define RX_PIN_IAP 35       // uart rx to iap
#define TX_PIN_MONITOR 1    // uart to monitor
#define RX_PIN_MONITOR 3
typedef enum {
    Boot_State_Idle = 0,
    Boot_State_Send_New_Firmware = 1,
    Boot_State_Jump_To_User_App = 2,
    Boot_State_Get_Basic_Info = 3,
    Boot_State_Waiting_IAP_Ready_Receive_New_Firmware = 4,
    Boot_State_Prepare_Download_Fw = 5,
    Boot_State_default = 0xff
}Bootloader_State;
/**************************************************/
Bootloader_State boot_state;
HardwareSerial monitorPort(2);
HardwareSerial IAPPort(1);
File hexFile;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
byte buffer[20];
bool is_error = false;
const int LED = 5;
const char* ssid = "Peco T7";
const char* ssid_ap = "ESP32_AP";
const char* password = "123456$@";
const char* host = "esp32";
uint32_t updateCounter = 0;
String version = "1.8";
String key = "aecd9f79-3034-4d75-b184-76a9b048a2c2";
bool is_update = false;
t_httpUpdate_return status;
unsigned long lastTime = 0;
unsigned long timerDelay = 30000;
bool is_hex_file = false;
bool is_write_sdcard = false;
bool is_file_exist = false;
uint16_t line = 0;
uint16_t totalLines = 0;
const char index_html[] PROGMEM = ""
" <!DOCTYPE html>"
" <html lang=\"en\">"
" <head>"
    " <meta charset=\"UTF-8\">"
    " <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    " <title>WebSocket File Upload</title>"
"</head>"
"<body>"
    "<input type=\"file\" id=\"fileInput\" />"
    "<button id =\"uploadBtn\" onclick=\"uploadFile()\" disabled=\"disabled\" >Upload</button>"
    "<progress id=\"progressBar\" max=\"100\" value=\"0\"></progress>"
    "<script>"
        "var uploadBtn = document.getElementById('uploadBtn');"
        "var url = window.location.host;"
        "var socket = new WebSocket('ws://' + url + '/ws');"
        "console.log('ws://' + url + '/ws');"
        "socket.addEventListener('open', function (event) {"
            "console.log('Connected to server.');"
            "uploadBtn.disabled = false;"
        "});"
        "socket.addEventListener('close', function (event) {"
            "console.log('Connection closed.');"
        "});"
        "function uploadFile() {"
            "var fileInput = document.getElementById('fileInput');"
            "var file = fileInput.files[0];"
            "var progressBar = document.getElementById('progressBar');"
            "if (file) {"
                "var reader = new FileReader();"
                "socket.send('HEX_FILE');"
                "reader.onload = function (event) {"
                    "var fileContent = event.target.result;"
                    "var lines = fileContent.split(String.fromCharCode(0x0A));"
                    "var totalLines = lines.length;"
                    "socket.send(totalLines);"
                    "var linesSent = 0;"
                    "lines.forEach(function (line, index) {"
                        "setTimeout(function () {"
                            "sendLineToServer(line.trim());"
                            "linesSent++;"
                            "var percentComplete = (linesSent / totalLines) * 100;"
                            "progressBar.value = percentComplete;"
                        "}, index * 100);"
                    "});"
                "};"
                "reader.onprogress = function(event) {"
                    "console.log('kkk');"
                    "if (event.lengthComputable) {"
                    "var percentLoaded = (event.loaded / event.total) * 100;"
                    "console.log('File loading progress:', percentLoaded + '%');"
                    "}"
                "};"
                "reader.readAsText(file, 'utf-8');"
            "}"
        "}"
        "function sendLineToServer(line) {"
            "socket.send(line);"
            "console.log('Sending line to server:', line);"
        "}"
    "</script>"
"</body>"
"</html>";
/**************************************************/
void user_app();
t_httpUpdate_return update_FOTA();
void initWiFi() {
#if 1
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
#else
  WiFi.softAP(ssid_ap, password);
  delay(1000);
  Serial.println(WiFi.softAPIP());
#endif
  if (!MDNS.begin(host)) { //http://esp32.local
    Serial.println("Error setting up MDNS responder!");
  }
}
void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}
void initSDCard() {
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);             
  if (!SD.begin(SD_CS)) {                                  
    Serial.println("SDCard MOUNT FAIL");
  } else {
    Serial.println("SDCard ready");
    uint32_t cardSize = SD.cardSize() / (1024 * 1024);
    String str = "SDCard Size: " + String(cardSize) + "MB";
    Serial.println(str);
    File root;
    root = SD.open("/");
    printDirectory(root, 0);
  }
}
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  Serial.print("has message");
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
  }
}
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA && len > 0) { // type: loại sự kiện mà server nhận được. Nếu sự kiện nhận được là từ websocket thì bắt đầu xử lí
    data[len] = 0;
    String data_str = String((char*)data);
    if (data_str == "HEX_FILE"){
      is_hex_file = true;
      data_str = "";
      return;
    }
    if(is_hex_file){
      totalLines = data_str.toInt() - 1;
      Serial.print(totalLines);
      is_hex_file = false;
      is_write_sdcard = true;
      data_str = "";
      return;
    }
    if(is_write_sdcard){
      if(line == totalLines){
        is_write_sdcard = false;
        is_file_exist = false;
        line = 0;
        return;
      }
      if(!is_file_exist){
        hexFile = SD.open("/user_app.hex", FILE_WRITE);
        is_file_exist = true;
      }else{
        hexFile = SD.open("/user_app.hex", FILE_APPEND);
      }
      if (hexFile) {
        Serial.print(data_str);
        hexFile.println(data_str);
        Serial.print('\n');
        line++;
        hexFile.close();
      }else{
        Serial.print("error when open /user_app.hex");
      }
      data_str = "";
      return;
    }
  }
}

void printDirectory(File dir, int numTabs) {
  while (true) {
    File entry =  dir.openNextFile();
    if (! entry) {
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}

String getChipId()
{
  String ChipIdHex = String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
  ChipIdHex += String((uint32_t)ESP.getEfuseMac(), HEX);
  return ChipIdHex;
}

t_httpUpdate_return update_FOTA()
{
  String url = "http://otadrive.com/deviceapi/update?";
  url += "k=" + key;
  url += "&v=" + version;
  url += "&s=" + getChipId();
  Serial.println("update_FOTA");
  WiFiClient client;
  t_httpUpdate_return ret = httpUpdate.update(client, url, version);
  return ret;
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BTN_FOTA, INPUT);
  boot_state = Boot_State_Idle;
  monitorPort.begin(9600, SERIAL_8N1,RX_PIN_MONITOR, TX_PIN_MONITOR);
  IAPPort.begin(9600,SERIAL_8N1,RX_PIN_IAP,TX_PIN_IAP);
  pinMode(LED, OUTPUT);
  Serial.begin(115200);
  Serial.print("Ver: ");
  Serial.println(version);
  initWiFi();
  initWebSocket();
  initSDCard();
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html); 
  });
  server.begin();
  if (WiFi.status() == WL_CONNECTED)
  {
    int buttonStatus = digitalRead(BTN_FOTA); 
    if(buttonStatus == HIGH){
      Serial.println("Check update");
      is_update = true;
    }
  }
}
void loop()
{
  if(is_update){
    status = update_FOTA();
    Serial.println(status);
    switch (status) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
        break;
      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        is_update = false;
        ESP.restart();
        break;
      case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK");
        is_update = false;
        ESP.restart();
        break;
    }
  }else{
    user_app();
  }
}

void user_app(){
  ws.cleanupClients();
  if (Serial.available()){
    byte rx_data = Serial.read();
    Serial.print("Serial receive: ");
    Serial.print(rx_data);
    switch(rx_data){
      case 0x01:
      boot_state = Boot_State_Prepare_Download_Fw;
      Serial.println("Boot_State_Prepare_Download_Fw.");
      rx_data = 0xFF;
      break;
      case 0x02:
      boot_state = Boot_State_Get_Basic_Info;
      Serial.println("Boot_State_Get_Basic_Info.");
      rx_data = 0xFF;
      break;
      case 0x03:
      boot_state = Boot_State_Jump_To_User_App;
      Serial.println("Boot_State_Jump_To_User_App.");
      rx_data = 0xFF;
      break;
      default:
      break;
    }
  }
  if (IAPPort.available()){
    Serial.println("IAP Send:");
    String str = IAPPort.readString();
    Serial.println(str);
  }
  switch(boot_state){
    case Boot_State_Idle:
      break;
    case Boot_State_Send_New_Firmware:
    {
      boot_state = Boot_State_Idle;
      File dataFile = SD.open("/user_app.hex", FILE_READ);
      if (dataFile) {
        Serial.println("Open success.");
        while (dataFile.available() && is_error == false) {
          IAPPort.write(dataFile.read());///IAPPort
          if (IAPPort.available()){///IAPPort
            char rx_data = IAPPort.read();///IAPPort
            Serial.println("IAP Send:");
            Serial.println(rx_data);
            if(rx_data == '>'){
              Serial.print(">");
            }else{
              IAPPort.readBytesUntil('\n', buffer, 20);///IAPPort
              File logFile = SD.open("LOG.txt", FILE_WRITE);
              is_error = true;
              Serial.print("is_error = true");
              break;
            }
          }
        }
      }
      else {
        Serial.println("Error open user_app.hex");
      }
      dataFile.close();
      break;
    }
    case Boot_State_Jump_To_User_App:
      boot_state = Boot_State_Idle;
      IAPPort.write(0x03);
      break;
    case Boot_State_Get_Basic_Info:
      boot_state = Boot_State_Idle;
      IAPPort.write(0x02);
      break;
    case Boot_State_Prepare_Download_Fw:
      boot_state = Boot_State_Waiting_IAP_Ready_Receive_New_Firmware;
      IAPPort.write(0x01);
      delay(200);
      break;
    case Boot_State_Waiting_IAP_Ready_Receive_New_Firmware:
      // if(_port.getRxBuffer().contains("Waiting new firmware")){
        boot_state = Boot_State_Send_New_Firmware;
      // }
      break;
    default:
      break;
  }
}