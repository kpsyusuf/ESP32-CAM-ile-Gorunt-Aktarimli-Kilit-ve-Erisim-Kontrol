/*
 * =====================================================
 *  AKILLI KAPI KİLİT SİSTEMİ - ESP32-CAM v12
 *  system_settings tablosu entegrasyonu eklendi
 * =====================================================
 */

#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "SD_MMC.h"
#include <time.h>

#define WIFI_SSID       "TECNO CAMON 20 Pro"
#define WIFI_PASS       "qhb234jab"
#define SUPABASE_URL    "https://rsdmlfokksixotaukxqa.supabase.co"
#define SUPABASE_KEY    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InJzZG1sZm9ra3NpeG90YXVreHFhIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzI1MjYzNDYsImV4cCI6MjA4ODEwMjM0Nn0.HQ7bEFWCNkombqUc8eC1jPYqrCgBCmpnmyMY06U2aGg"
#define DEVICE_SERIAL   "ESP32CAM_001"

#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22
#define FLASH_PIN        4

#define PHOTO_INTERVAL_MS   10000UL
#define SENSOR_INTERVAL_MS  60000UL
#define CMD_CHECK_MS         5000UL
#define WIFI_CHECK_MS       10000UL
#define SETTINGS_CHECK_MS   30000UL  // 30sn'de bir sistem ayarlarını kontrol et
#define MAX_PHOTOS             12

// Sensör verileri
float temperature=0, humidity=0;
int   distance=0, lightLevel=0;
bool  nightMode=false, wifiConnected=false;
bool  sdReady=false, camReady=false;
int   photoIndex=0;

// Veritabanından çekilen cihaz ayarları
String pinCode="1234";
int    maxHata=3;
int    alarmSuresi=300;

// Sistem ayarları
bool maintenanceMode=false;
bool notificationsEnabled=true;

// Cihaz durumu
bool isLocked=false;
bool doorOpen=false;

unsigned long lastPhotoTime=0, lastSensorSend=0;
unsigned long lastCmdCheck=0, lastWifiCheck=0;
unsigned long lastSettingsCheck=0;

String serialBuffer="";
String pushToken="";

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(9600);

  pinMode(FLASH_PIN, OUTPUT);
  digitalWrite(FLASH_PIN, LOW);

  // SD Kart - 1-bit mod
  if (SD_MMC.begin("/sdcard", true)) {
    sdReady = true;
  }

  // Kamera başlat
  for (int i=0; i<3; i++) {
    if (initCamera()) { camReady=true; break; }
    delay(1000);
  }

  digitalWrite(FLASH_PIN, LOW);

  connectWiFi();

  if (wifiConnected) {
    configTime(10800, 0, "pool.ntp.org");
    delay(1000);
    registerDevice();
    fetchSystemSettings();
    fetchDeviceSettings();
    fetchPushToken();
    sendSettingsToArduino();
  }
}

void loop() {
  unsigned long now = millis();

  if (!nightMode) digitalWrite(FLASH_PIN, LOW);

  // Arduino mesaj oku
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n') {
      serialBuffer.trim();
      if (serialBuffer.length() > 0) handleArduinoMessage(serialBuffer);
      serialBuffer = "";
    } else if (c != '\r') serialBuffer += c;
  }

  // Bakım modunda fotoğraf çekme
  if (!maintenanceMode && camReady && sdReady && now - lastPhotoTime >= PHOTO_INTERVAL_MS) {
    lastPhotoTime = now;
    captureToSD();
  }

  // Sensör verisi gönder
  if (wifiConnected && now - lastSensorSend >= SENSOR_INTERVAL_MS) {
    lastSensorSend = now;
    sendSensorToSupabase();
  }

  // Komut kontrol
  if (wifiConnected && now - lastCmdCheck >= CMD_CHECK_MS) {
    lastCmdCheck = now;
    checkSupabaseCommands();
  }

  // Sistem ayarları kontrol (30sn'de bir)
  if (wifiConnected && now - lastSettingsCheck >= SETTINGS_CHECK_MS) {
    lastSettingsCheck = now;
    fetchSystemSettings();
    fetchDeviceSettings();
    sendSettingsToArduino();
  }

  // WiFi kontrolü
  if (now - lastWifiCheck >= WIFI_CHECK_MS) {
    lastWifiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      wifiConnected = false;
      connectWiFi();
      if (wifiConnected) {
        fetchSystemSettings();
        fetchDeviceSettings();
        fetchPushToken();
        sendSettingsToArduino();
      }
    }
  }
}

// Arduino'ya ayarları gönder
void sendSettingsToArduino() {
  Serial.println("SET:PIN:" + pinCode);
  delay(100);
  Serial.println("SET:MAX_HATA:" + String(maxHata));
  delay(100);
  Serial.println("SET:ALARM_SURESI:" + String(alarmSuresi));
  delay(100);
}

void handleArduinoMessage(String msg) {
  // Bakım modunda sadece sensör verisini al
  if (maintenanceMode) {
    if (msg.startsWith("SENSOR:")) parseSensorData(msg);
    return;
  }

  if (msg.startsWith("SENSOR:")) { parseSensorData(msg); return; }
  if (msg == "NIGHT_MODE:ON")  { nightMode=true; return; }
  if (msg == "NIGHT_MODE:OFF") { nightMode=false; digitalWrite(FLASH_PIN, LOW); return; }
  if (msg == "ESP32:FLASH_ON") { digitalWrite(FLASH_PIN, HIGH); return; }
  if (msg == "ESP32:FLASH_OFF" || msg == "ESP32:FLASH_STANDBY") { digitalWrite(FLASH_PIN, LOW); return; }

  if (msg == "LOCK:ACTIVATED") {
    isLocked = true;
    updateDeviceStatus(true, doorOpen);
    uploadAllSDPhotos("alarm_max_attempts");
    sendPush("Sistem kilitlendi! Fotograflar yuklendi");
    return;
  }

  if (msg == "LOCK:EXPIRED" || msg == "LOCK:RESET_OK") {
    isLocked = false;
    updateDeviceStatus(false, doorOpen);
    return;
  }

  if (msg == "DOOR:OPEN") {
    doorOpen = true;
    updateDeviceStatus(isLocked, true);
    logEvent("door_open", "Kapi acildi", "info", "");
    sendPush("Kapı açıldı");
    return;
  }

  if (msg == "DOOR:CLOSED") {
    doorOpen = false;
    updateDeviceStatus(isLocked, false);
    logEvent("door_closed", "Kapi kapandi", "info", "");
    sendPush("Kapı kapandı");
    return;
  }

  if (msg.startsWith("ACCESS_DENIED:")) {
    int attempt = msg.substring(14).toInt();
    String url = captureAndUploadDirect();
    logEvent("access_denied", "Yanlis sifre - "+String(attempt)+". deneme", "warning", url);
    uploadAllSDPhotos("access_denied");
    sendPush("Yanlış şifre denemesi - "+String(attempt)+". deneme");
    return;
  }

  if (msg == "ESP32:ALARM_PHOTO" || msg == "ESP32:URGENT_PHOTO") {
    uploadAllSDPhotos("alarm");
    return;
  }

  if (msg.startsWith("ALARM:VIB")) {
    uploadAllSDPhotos("alarm_vib");
    sendPush("Kapiya dokunuldu, fotograf cekildi");
    return;
  }
}

void parseSensorData(String msg) {
  String data = msg.substring(7);
  int c1=data.indexOf(','), c2=data.indexOf(',',c1+1), c3=data.indexOf(',',c2+1);
  temperature = data.substring(0,c1).toFloat();
  humidity    = data.substring(c1+1,c2).toFloat();
  distance    = data.substring(c2+1,c3).toInt();
  lightLevel  = data.substring(c3+1).toInt();
}

void captureToSD() {
  if (!camReady || !sdReady) return;
  digitalWrite(FLASH_PIN, LOW);

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  String filename = "/photo_" + String(photoIndex) + ".jpg";
  if (SD_MMC.exists(filename)) SD_MMC.remove(filename);

  File file = SD_MMC.open(filename, FILE_WRITE);
  if (file) { file.write(fb->buf, fb->len); file.close(); }

  esp_camera_fb_return(fb);
  photoIndex = (photoIndex + 1) % MAX_PHOTOS;
}

void uploadAllSDPhotos(String eventType) {
  if (!wifiConnected) return;

  String directUrl = captureAndUploadDirect();
  logEvent(eventType, "Anlik fotograf", "critical", directUrl);

  int uploadCount = 0;
  for (int i=0; i<MAX_PHOTOS; i++) {
    String filename = "/photo_" + String(i) + ".jpg";
    if (SD_MMC.exists(filename)) {
      String url = uploadSDPhotoToSupabase(filename);
      if (url != "") {
        logEvent(eventType, "SD foto "+String(i), "critical", url);
        uploadCount++;
      }
    }
  }

  if (uploadCount > 0) sendPush(String(uploadCount) + " fotograf yuklendi");
}

String uploadSDPhotoToSupabase(String filename) {
  File file = SD_MMC.open(filename, FILE_READ);
  if (!file) return "";

  size_t fileSize = file.size();
  uint8_t *buf = (uint8_t*)malloc(fileSize);
  if (!buf) { file.close(); return ""; }
  file.read(buf, fileSize);
  file.close();

  String remoteName = "sd_" + String(millis()) + ".jpg";
  String uploadUrl = String(SUPABASE_URL) + "/storage/v1/object/door-photos/" + remoteName;

  HTTPClient http;
  http.begin(uploadUrl);
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(15000);
  int code = http.POST(buf, fileSize);
  free(buf);
  http.end();

  if (code==200 || code==201)
    return String(SUPABASE_URL) + "/storage/v1/object/public/door-photos/" + remoteName;
  return "";
}

String captureAndUploadDirect() {
  if (!wifiConnected || !camReady) return "";
  digitalWrite(FLASH_PIN, LOW);
  delay(100);

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    delay(500);
    fb = esp_camera_fb_get();
    if (!fb) return "";
  }

  String remoteName = String(millis()) + ".jpg";
  String uploadUrl = String(SUPABASE_URL) + "/storage/v1/object/door-photos/" + remoteName;

  HTTPClient http;
  http.begin(uploadUrl);
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(15000);
  int code = http.POST(fb->buf, fb->len);
  esp_camera_fb_return(fb);
  http.end();

  if (code==200 || code==201)
    return String(SUPABASE_URL) + "/storage/v1/object/public/door-photos/" + remoteName;
  return "";
}

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0=Y2_GPIO_NUM; config.pin_d1=Y3_GPIO_NUM;
  config.pin_d2=Y4_GPIO_NUM; config.pin_d3=Y5_GPIO_NUM;
  config.pin_d4=Y6_GPIO_NUM; config.pin_d5=Y7_GPIO_NUM;
  config.pin_d6=Y8_GPIO_NUM; config.pin_d7=Y9_GPIO_NUM;
  config.pin_xclk=XCLK_GPIO_NUM; config.pin_pclk=PCLK_GPIO_NUM;
  config.pin_vsync=VSYNC_GPIO_NUM; config.pin_href=HREF_GPIO_NUM;
  config.pin_sscb_sda=SIOD_GPIO_NUM; config.pin_sscb_scl=SIOC_GPIO_NUM;
  config.pin_pwdn=PWDN_GPIO_NUM; config.pin_reset=RESET_GPIO_NUM;
  config.xclk_freq_hz=20000000; config.pixel_format=PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size=FRAMESIZE_VGA; config.jpeg_quality=10; config.fb_count=2;
  } else {
    config.frame_size=FRAMESIZE_CIF; config.jpeg_quality=12; config.fb_count=1;
  }

  bool ok = (esp_camera_init(&config) == ESP_OK);

  if (ok) {
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
      s->set_vflip(s, 1);
      s->set_hmirror(s, 1);
    }
  }

  digitalWrite(FLASH_PIN, LOW);
  return ok;
}

void logEvent(String eventType, String description, String alarmLevel, String imageUrl) {
  if (!wifiConnected) return;
  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/logs");
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> doc;
  doc["device_serial"]=DEVICE_SERIAL; doc["event_type"]=eventType;
  doc["description"]=description; doc["alarm_level"]=alarmLevel;
  doc["image_url"]=imageUrl; doc["temperature"]=temperature; doc["humidity"]=humidity;
  JsonObject snap=doc.createNestedObject("sensor_snapshot");
  snap["temperature"]=temperature; snap["humidity"]=humidity;
  snap["distance"]=distance; snap["light"]=lightLevel;

  String body; serializeJson(doc,body);
  http.POST(body); http.end();
}

// Sistem ayarlarını çek
void fetchSystemSettings() {
  if (!wifiConnected) return;
  HTTPClient http;
  http.begin(String(SUPABASE_URL)+"/rest/v1/system_settings?id=eq.1");
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
  http.addHeader("apikey", SUPABASE_KEY);

  int code = http.GET();
  if (code==200) {
    String resp = http.getString();
    StaticJsonDocument<256> doc;
    deserializeJson(doc, resp);
    if (doc.is<JsonArray>() && doc.size()>0) {
      maintenanceMode      = doc[0]["maintenance_mode"].as<bool>();
      notificationsEnabled = doc[0]["notifications_enabled"].as<bool>();
    }
  }
  http.end();
}

// Cihaz ayarlarını çek
void fetchDeviceSettings() {
  if (!wifiConnected) return;
  HTTPClient http;
  http.begin(String(SUPABASE_URL)+"/rest/v1/devices?device_serial=eq."+String(DEVICE_SERIAL)+"&select=pin_code,settings");
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
  http.addHeader("apikey", SUPABASE_KEY);

  int code = http.GET();
  if (code==200) {
    String resp = http.getString();
    StaticJsonDocument<512> doc;
    deserializeJson(doc, resp);
    if (doc.is<JsonArray>() && doc.size()>0) {
      if (!doc[0]["pin_code"].isNull()) {
        pinCode = doc[0]["pin_code"].as<String>();
      }
      JsonObject settings = doc[0]["settings"];
      if (settings.containsKey("max_hata"))    maxHata    = settings["max_hata"].as<int>();
      if (settings.containsKey("alarm_suresi")) alarmSuresi = settings["alarm_suresi"].as<int>();
    }
  }
  http.end();
}

// Cihaz durumunu güncelle
void updateDeviceStatus(bool locked, bool door) {
  if (!wifiConnected) return;
  HTTPClient http;
  http.begin(String(SUPABASE_URL)+"/rest/v1/devices?device_serial=eq."+String(DEVICE_SERIAL));
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  StaticJsonDocument<128> doc;
  doc["is_locked"] = locked;
  doc["door_open"] = door;

  String body; serializeJson(doc,body);
  http.PATCH(body); http.end();
}

void registerDevice() {
  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/devices");
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "resolution=merge-duplicates");

  StaticJsonDocument<256> doc;
  doc["device_serial"]=DEVICE_SERIAL;
  doc["device_name"]="Akilli Kapi ESP32CAM";
  doc["is_active"]=true;
  doc["last_seen"]=getTimestamp();

  String body; serializeJson(doc,body);
  http.POST(body); http.end();
}

void sendSensorToSupabase() {
  HTTPClient http;
  http.begin(String(SUPABASE_URL)+"/rest/v1/devices?device_serial=eq."+String(DEVICE_SERIAL));
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  StaticJsonDocument<256> doc;
  doc["last_temperature"]=temperature; doc["last_humidity"]=humidity;
  doc["last_distance"]=distance; doc["last_light"]=lightLevel;
  doc["night_mode"]=nightMode; doc["last_seen"]=getTimestamp();

  String body; serializeJson(doc,body);
  http.PATCH(body); http.end();
}

void fetchPushToken() {
  if (!wifiConnected) return;
  HTTPClient http;
  http.begin(String(SUPABASE_URL)+"/rest/v1/users?select=expo_push_token&limit=1");
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
  http.addHeader("apikey", SUPABASE_KEY);

  int code = http.GET();
  if (code==200) {
    String resp = http.getString();
    StaticJsonDocument<256> doc;
    deserializeJson(doc, resp);
    if (doc.is<JsonArray>() && doc.size()>0) {
      String token = doc[0]["expo_push_token"].as<String>();
      if (token != "null" && token != "") pushToken = token;
    }
  }
  http.end();
}

void sendPush(String message) {
  if (!wifiConnected || pushToken=="" || !notificationsEnabled) return;
  HTTPClient http;
  http.begin("https://exp.host/--/api/v2/push/send");
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<256> doc;
  doc["to"]=pushToken; doc["title"]="Akıllı Kapı";
  doc["body"]=message; doc["sound"]="default";
  String body; serializeJson(doc,body);
  http.POST(body); http.end();
}

void checkSupabaseCommands() {
  HTTPClient http;
  http.begin(String(SUPABASE_URL)+"/rest/v1/devices?device_serial=eq."+String(DEVICE_SERIAL)+"&select=settings");
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
  http.addHeader("apikey", SUPABASE_KEY);

  int code = http.GET();
  if (code==200) {
    String resp = http.getString();
    StaticJsonDocument<512> doc;
    deserializeJson(doc, resp);
    if (doc.is<JsonArray>() && doc.size()>0) {
      JsonObject settings = doc[0]["settings"];
      if (settings.containsKey("pending_cmd")) {
        String cmd = settings["pending_cmd"].as<String>();
        if (cmd=="ALARM_RESET") {
          Serial.println("CMD:ALARM_RESET");
          clearPendingCommand();
        } else if (cmd=="LOCK_RESET") {
          Serial.println("CMD:LOCK_RESET");
          isLocked = false;
          updateDeviceStatus(false, doorOpen);
          clearPendingCommand();
        } else if (cmd=="DOOR_OPEN") {
          Serial.println("CMD:DOOR_OPEN");
          clearPendingCommand();
        }
      }
    }
  }
  http.end();
}

void clearPendingCommand() {
  HTTPClient http;
  http.begin(String(SUPABASE_URL)+"/rest/v1/devices?device_serial=eq."+String(DEVICE_SERIAL));
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<128> settingsDoc;
  settingsDoc["max_hata"] = maxHata;
  settingsDoc["alarm_suresi"] = alarmSuresi;

  StaticJsonDocument<256> body;
  body["settings"] = settingsDoc;
  String bodyStr; serializeJson(body, bodyStr);

  http.PATCH(bodyStr);
  http.end();
}

void connectWiFi() {
  WiFi.disconnect(true);
  delay(1000);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempt=0;
  while (WiFi.status()!=WL_CONNECTED && attempt<30) { delay(500); attempt++; }
  wifiConnected = (WiFi.status()==WL_CONNECTED);
}

String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return String(millis());
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S+03:00", &timeinfo);
  return String(buf);
}
