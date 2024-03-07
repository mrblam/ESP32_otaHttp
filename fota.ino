#include <Arduino.h>
#include <HTTPUpdate.h>
 
#define LED_BUILTIN   5 
#define BTN_ROLLBACK   36 
uint32_t updateCounter = 0;
String version = "1.7";
String key = "aecd9f79-3034-4d75-b184-76a9b048a2c2";
bool is_update = false;
t_httpUpdate_return status;
t_httpUpdate_return update_FOTA();
void user_app();
void setup()
{
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BTN_ROLLBACK, INPUT);
  WiFi.begin("Peco T7", "123456$@");
  Serial.println("Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.localIP());
  /***/
  Serial.print("Ver: ");
  Serial.println(version);
  delay(500);
  if (WiFi.status() == WL_CONNECTED)
  {
    int buttonStatus = digitalRead(BTN_ROLLBACK); 
    if(buttonStatus == HIGH){
      Serial.println("Check update");
      is_update = true;
      // update_FOTA();
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

void user_app(){
  digitalWrite(LED_BUILTIN, HIGH); 
  delay(1000);                   
  digitalWrite(LED_BUILTIN, LOW);   
  delay(1000);
}