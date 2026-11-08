#include <WiFi.h>
#include <esp_wifi.h>


// 'A' on the first ESP32, 'B' on the second. Each one broadcasts its own Wi-Fi.
static const char BEACON_ID = 'B';
static const int AP_CHANNEL = 1;
static const char *AP_PASSWORD = "cheesieee";
// Onboard blue LED. Leave it low until setup, then hold it on.
static const int LED_PIN = 2;


static char apSsid[16];


void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);


  Serial.begin(115200);
  delay(300);
  digitalWrite(LED_PIN, HIGH);


  snprintf(apSsid, sizeof(apSsid), "CanvU-Axis%c", BEACON_ID);


  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  // Hidden and locked. The name is carried in a tag only the hub looks for.
  WiFi.softAP(apSsid, AP_PASSWORD, AP_CHANNEL, 1, 1);
  esp_wifi_set_ps(WIFI_PS_NONE);


  uint8_t tag[16];
  tag[0] = 0xDD;
  tag[1] = 14;
  tag[2] = 'C';
  tag[3] = 'V';
  tag[4] = 'U';
  memcpy(tag + 5, apSsid, 11);
  esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_BEACON, WIFI_VND_IE_ID_0, tag);


  Serial.print("broadcasting ");
  Serial.println(apSsid);
}


void loop() {
}





