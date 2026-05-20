/*
 * =====================================================
 *  AKILLI KAPI KİLİT SİSTEMİ - Arduino Uno v9
 *  RAM optimizasyonu - String yerine char array
 * =====================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Keypad.h>
#include <Servo.h>
#include <IRremote.hpp>

#define DHT_PIN     2
#define VIB_PIN     3
#define TRIG_PIN    4
#define ECHO_PIN    5
#define IR_PIN      6
#define KR1         7
#define KR2         8
#define KR3         9
#define KR4         10
#define KC1         11
#define KC2         12
#define LDR_PIN     A0
#define SERVO_PIN   A1
#define BUZZER_PIN  A2
#define KC3         A3

LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHT_PIN, DHT11);
Servo doorServo;

const byte ROWS = 4, COLS = 3;
char keys[ROWS][COLS] = {
  {'1','2','3'},{'4','5','6'},{'7','8','9'},{'*','0','#'}
};
byte rowPins[ROWS] = {KR1,KR2,KR3,KR4};
byte colPins[COLS] = {KC1,KC2,KC3};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Ayarlar - char array kullan (String yerine)
char BASE_PASS[9]     = "1234";   // max 8 hane + null
int  MAX_FAIL         = 3;
long LOCK_DURATION_MS = 300000UL;

const int  DOOR_OPEN_ANGLE   = 90;
const int  DOOR_CLOSED_ANGLE = 0;
const int  PROX_NORMAL_CM    = 80;
const int  PROX_NIGHT_CM     = 40;
const int  DOOR_OPEN_MS      = 5000;
const long SENSOR_INTERVAL_MS = 60000UL;
const long ACTIVE_CHECK_MS    = 60000UL;
const long PASSIVE_CHECK_MS   = 3000UL;
const int  LDR_NIGHT_THRESH   = 20;

float temperature = 25.0;
float humidity    = 50.0;
int   lightLevel  = 70;
long  distance    = 200;

bool doorOpen     = false;
bool systemActive = false;
bool nightMode    = false;
bool alarmActive  = false;
bool locked       = false;

int  failCount    = 0;
char entered[9]   = "";   // max 6 hane + null
char serialBuf[64] = "";  // Serial buffer - sabit boyut
byte serialIdx    = 0;

bool lastVibState = HIGH;
unsigned long vibDebounce  = 0;
unsigned long lastDistCheck = 0;
unsigned long lastIRTime    = 0;
unsigned long lastLcdUpdate = 0;
unsigned long doorOpenTime  = 0;
unsigned long lockStartTime = 0;
unsigned long sensorDue     = 0;

void setup() {
  Serial.begin(9600);
  pinMode(TRIG_PIN,   OUTPUT);
  pinMode(ECHO_PIN,   INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(VIB_PIN,    INPUT_PULLUP);

  doorServo.attach(SERVO_PIN);
  doorServo.write(DOOR_CLOSED_ANGLE);

  lcd.init();
  lcd.init();  // İki kez init - LCD sorununu önler
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(3,0); lcd.print(F("AKILLI KAPI"));
  lcd.setCursor(2,1); lcd.print(F("Baslatiliyor..."));

  dht.begin();
  IrReceiver.begin(IR_PIN, DISABLE_LED_FEEDBACK);

  delay(2000);
  readSensors();
  sensorDue = millis() + SENSOR_INTERVAL_MS;
  lcd.clear();
  beep(2,100);
  Serial.println(F("SYSTEM_READY"));
}

void loop() {
  unsigned long now = millis();
  if (locked) { handleLocked(now); return; }

  // Titreşim
  bool vibState = digitalRead(VIB_PIN);
  if (vibState==LOW && lastVibState==HIGH && now-vibDebounce>50) {
    vibDebounce = now;
    if (!doorOpen) triggerAlarm();
  }
  lastVibState = vibState;

  // ESP32 komut oku - polling, delay yok
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n') {
      serialBuf[serialIdx] = '\0';
      serialIdx = 0;
      if (strlen(serialBuf) > 0) handleESPCommand(serialBuf);
    } else if (c != '\r' && serialIdx < 63) {
      serialBuf[serialIdx++] = c;
    }
  }

  // Mesafe ölçümü
  long checkInterval = systemActive ? ACTIVE_CHECK_MS : PASSIVE_CHECK_MS;
  if (now - lastDistCheck >= checkInterval) {
    lastDistCheck = now;
    distance = readDistance();
    int proxThresh = nightMode ? PROX_NIGHT_CM : PROX_NORMAL_CM;
    bool personClose = (distance > 0 && distance < proxThresh);

    if (personClose && !systemActive) {
      systemActive = true;
      lcd.backlight();
      if (nightMode) Serial.println(F("ESP32:FLASH_ON"));
      readSensors();
      sendSensorData();
      sensorDue = now + SENSOR_INTERVAL_MS;
      beep(1,80);
      Serial.println(F("SYSTEM_ACTIVATED"));
    }
    if (systemActive && !personClose && !doorOpen && strlen(entered)==0) {
      systemActive = false;
      if (nightMode) Serial.println(F("ESP32:FLASH_OFF"));
      Serial.println(F("SYSTEM_STANDBY"));
    }
  }

  // Sensör yenileme
  if (now >= sensorDue && !doorOpen) {
    if (strlen(entered) > 0) {
      sensorDue = now + SENSOR_INTERVAL_MS;
    } else {
      readSensors();
      sendSensorData();
      sensorDue = now + SENSOR_INTERVAL_MS;
    }
  }

  // LDR gece modu - delay olmadan
  lightLevel = map(analogRead(LDR_PIN), 0, 1023, 0, 100);
  bool wasNight = nightMode;
  nightMode = (lightLevel < LDR_NIGHT_THRESH);
  if (nightMode != wasNight) {
    if (nightMode) {
      Serial.println(F("NIGHT_MODE:ON"));
      Serial.println(F("ESP32:FLASH_STANDBY"));
    } else {
      Serial.println(F("NIGHT_MODE:OFF"));
    }
  }

  // Kapı otomatik kapat
  if (doorOpen && now - doorOpenTime >= DOOR_OPEN_MS) closeDoor();

  // LCD güncelle
  if (now - lastLcdUpdate >= 500) {
    updateLCD();
    lastLcdUpdate = now;
  }

  // Keypad
  char key = keypad.getKey();
  if (key) handleKey(key);

  // IR
  if (IrReceiver.decode()) {
    uint8_t cmd   = IrReceiver.decodedIRData.command;
    uint8_t proto = IrReceiver.decodedIRData.protocol;
    IrReceiver.resume();
    if (proto == 7 && cmd != 0) handleIR(cmd);
  }
}

void handleLocked(unsigned long now) {
  long rem = (long)(lockStartTime + LOCK_DURATION_MS) - (long)now;
  if (rem <= 0) {
    locked=false; failCount=0; alarmActive=false;
    lcd.clear(); beep(1,200);
    Serial.println(F("LOCK:EXPIRED"));
    return;
  }
  if (now - lastLcdUpdate >= 1000) {
    lcd.clear();
    lcd.setCursor(0,0); lcd.print(F("  KILITLI!"));
    lcd.setCursor(0,1);
    int sn = rem / 1000;
    lcd.print(F("Kal: "));
    lcd.print(sn/60); lcd.print(F("d "));
    lcd.print(sn%60); lcd.print(F("s   "));
    lastLcdUpdate = now;
  }
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n') {
      serialBuf[serialIdx] = '\0';
      serialIdx = 0;
      if (strlen(serialBuf) > 0) handleESPCommand(serialBuf);
    } else if (c != '\r' && serialIdx < 63) {
      serialBuf[serialIdx++] = c;
    }
  }
  if (IrReceiver.decode()) {
    uint8_t cmd   = IrReceiver.decodedIRData.command;
    uint8_t proto = IrReceiver.decodedIRData.protocol;
    IrReceiver.resume();
    if (proto == 7 && cmd != 0) handleIR(cmd);
  }
}

void handleESPCommand(char* cmd) {
  if (strncmp(cmd, "SET:PIN:", 8) == 0) {
    strncpy(BASE_PASS, cmd+8, 8);
    BASE_PASS[8] = '\0';
    return;
  }
  if (strncmp(cmd, "SET:MAX_HATA:", 13) == 0) {
    MAX_FAIL = atoi(cmd+13);
    return;
  }
  if (strncmp(cmd, "SET:ALARM_SURESI:", 17) == 0) {
    LOCK_DURATION_MS = (long)atoi(cmd+17) * 1000;
    return;
  }
  if (strcmp(cmd, "CMD:ALARM_RESET") == 0) {
    alarmActive = false; lcd.clear();
    Serial.println(F("ALARM:RESET_OK"));
  } else if (strcmp(cmd, "CMD:LOCK_RESET") == 0) {
    locked=false; failCount=0; alarmActive=false;
    lcd.clear(); beep(2,150);
    Serial.println(F("LOCK:RESET_OK"));
  } else if (strcmp(cmd, "CMD:DOOR_OPEN") == 0) {
    if (!doorOpen) openDoor();
  }
}

void readSensors() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h)) humidity    = h;
  if (!isnan(t)) temperature = t;
  lightLevel = map(analogRead(LDR_PIN), 0, 1023, 0, 100);
}

long readDistance() {
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 50000);
  return (dur == 0) ? 999 : dur * 0.034 / 2;
}

void sendSensorData() {
  Serial.print(F("SENSOR:"));
  Serial.print(temperature, 1); Serial.print(',');
  Serial.print(humidity, 1);    Serial.print(',');
  Serial.print(distance);       Serial.print(',');
  Serial.println(lightLevel);
}

// Dinamik şifre: BASE_PASS + nem_birler + sicaklik_birler
void getDynPass(char* out) {
  strcpy(out, BASE_PASS);
  int len = strlen(out);
  out[len]   = '0' + ((int)humidity % 10);
  out[len+1] = '0' + (abs((int)temperature) % 10);
  out[len+2] = '\0';
}

void handleKey(char key) {
  if (!systemActive) { systemActive=true; lcd.backlight(); }
  if (key == '*') {
    entered[0] = '\0';
    beep(1,50);
  } else if (key == '#') {
    checkPassword();
  } else if (strlen(entered) < 6) {
    int len = strlen(entered);
    entered[len]   = key;
    entered[len+1] = '\0';
    beep(1,40);
  }
}

void checkPassword() {
  char correct[9];
  getDynPass(correct);

  Serial.print(F("ATTEMPT:")); Serial.print(entered);
  Serial.print(F(" CORRECT:")); Serial.println(correct);

  if (strcmp(entered, correct) == 0) {
    failCount=0; alarmActive=false;
    entered[0] = '\0';
    openDoor();
    Serial.println(F("ACCESS_GRANTED"));
  } else {
    failCount++;
    entered[0] = '\0';
    beep(3,200);
    lcd.clear();
    lcd.setCursor(0,0); lcd.print(F("  YANLIS SIFRE!"));
    lcd.setCursor(0,1); lcd.print(F("Deneme:"));
    lcd.print(failCount); lcd.print('/'); lcd.print(MAX_FAIL);
    Serial.print(F("ACCESS_DENIED:")); Serial.println(failCount);

    if (failCount >= MAX_FAIL) {
      delay(800);
      activateLock();
    } else {
      delay(1500);
      readSensors();
      sendSensorData();
      sensorDue = millis() + SENSOR_INTERVAL_MS;
      Serial.println(F("SENSOR_REFRESH:AFTER_FAIL"));
    }
  }
}

void activateLock() {
  locked=true; lockStartTime=millis(); alarmActive=true;
  for (int i=0;i<8;i++) {
    digitalWrite(BUZZER_PIN,HIGH); delay(100);
    digitalWrite(BUZZER_PIN,LOW);  delay(100);
  }
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(F("!! KILITLENDI !!"));
  lcd.setCursor(0,1); lcd.print(F("Mobil veya IR"));
  Serial.println(F("LOCK:ACTIVATED"));
  Serial.println(F("ESP32:ALARM_PHOTO"));
}

void openDoor() {
  alarmActive = false;
  doorServo.write(DOOR_OPEN_ANGLE);
  doorOpen=true; doorOpenTime=millis();
  beep(2,150);
  lcd.clear();
  lcd.setCursor(2,0); lcd.print(F("KAPI ACILDI!"));
  lcd.setCursor(1,1); lcd.print(F("Hos geldiniz"));
  Serial.println(F("DOOR:OPEN"));
}

void closeDoor() {
  doorServo.write(DOOR_CLOSED_ANGLE);
  doorOpen=false; beep(1,100); lcd.clear();
  Serial.println(F("DOOR:CLOSED"));
}

void triggerAlarm() {
  alarmActive = true;
  Serial.println(F("ALARM:VIB"));
  Serial.println(F("ESP32:ALARM_PHOTO"));
  for (int i=0;i<5;i++) {
    digitalWrite(BUZZER_PIN,HIGH); delay(100);
    digitalWrite(BUZZER_PIN,LOW);  delay(100);
  }
  lcd.clear();
  lcd.setCursor(3,0); lcd.print(F("!! ALARM !!"));
  lcd.setCursor(0,1); lcd.print(F("Yetkisiz erisim!"));
}

void updateLCD() {
  if (doorOpen || locked || alarmActive) return;

  lcd.setCursor(0,0);
  lcd.print(F("M:"));
  if (distance < 400) {
    if (distance < 10)       lcd.print(F("  "));
    else if (distance < 100) lcd.print(' ');
    lcd.print(distance);
  } else {
    lcd.print(F("---"));
  }
  lcd.print(F(" N:"));
  if (humidity < 10) lcd.print(' ');
  lcd.print((int)humidity);
  lcd.print(F(" S:"));
  lcd.print((int)temperature);
  lcd.print(F("   "));

  lcd.setCursor(0,1);
  int elen = strlen(entered);
  for (int i=0; i<6; i++) {
    lcd.print(i < elen ? '*' : '_');
  }
  lcd.print(F("          "));
}

void beep(int n, int ms) {
  for (int i=0;i<n;i++) {
    digitalWrite(BUZZER_PIN,HIGH); delay(ms);
    digitalWrite(BUZZER_PIN,LOW);
    if (i<n-1) delay(60);
  }
}

void handleIR(uint8_t cmd) {
  unsigned long now = millis();
  if (now - lastIRTime < 5000) return;
  lastIRTime = now;
  switch(cmd) {
    case 0x45:
      if (!locked && !doorOpen) { openDoor(); Serial.println(F("IR:DOOR_OPEN")); }
      break;
    case 0x47:
      if (locked) {
        locked=false; failCount=0; alarmActive=false;
        lcd.clear(); beep(2,150);
        Serial.println(F("IR:LOCK_RESET"));
      }
      break;
    case 0x46:
      if (alarmActive) {
        alarmActive=false; lcd.clear();
        Serial.println(F("IR:ALARM_RESET"));
      }
      break;
  }
}
