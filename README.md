# ESP32-CAM ile Görüntü Aktarımlı Akıllı Kilit ve Erişim Kontrolü

Nesnelerin İnterneti (IoT) tabanlı, görüntü aktarımlı akıllı kapı kilidi ve erişim kontrol sistemi. Arduino Uno R3 ve ESP32-CAM modülünün birlikte çalışması esasına dayanır; geleneksel mekanik kilitlerin güvenlik açıklarını kapatmayı hedefler. DHT11 sensöründen okunan sıcaklık ve nem değerlerine bağlı, **her dakika otomatik yenilenen dinamik şifre** üretimi; yetkisiz erişim durumunda otomatik fotoğraf çekip buluta yükleme; ve mobil uygulama üzerinden uzaktan yönetim sunar.

> Muş Alparslan Üniversitesi · Yazılım Mühendisliği · Robotik Dersi Lisans Dönem Projesi (Haziran 2026)

## Özellikler

- **Dinamik şifre:** Sabit taban şifre + DHT11 sıcaklık/nem verisinden türetilen dinamik bileşen; şifre her dakika otomatik yenilenir.
- **4×3 keypad** ile şifre girişi ve servo motorlu kilit kontrolü.
- **Yaklaşma algılama:** HC-SR04 ultrasonik sensör ile kapıya yaklaşan kişilerin tespiti (gündüz/gece için ayrı mesafe eşikleri).
- **Sabotaj algılama:** SW-420 titreşim sensörü ile yetkisiz dokunma/zorlamanın anlık tespiti.
- **Gece modu:** LDR ile ortam ışığı ölçülerek gece modunun otomatik devreye girmesi.
- **Görüntü kaydı:** Yanlış şifre ve alarm durumlarında ESP32-CAM ile otomatik fotoğraf çekimi, SD karta dairesel buffer mantığıyla kayıt ve Supabase bulut depolamaya aktarım.
- **Anlık bildirim:** Expo Push Notification altyapısı ile mobil uygulamaya gerçek zamanlı uyarı.
- **Uzaktan yönetim:** Mobil uygulama üzerinden kapı açma, kilit sıfırlama ve sistem ayarlarını değiştirme.
- **IR alıcı** ile kumanda desteği.

## Sistem Mimarisi

```
┌─────────────────┐         ┌─────────────────┐
│   Arduino Uno    │  seri   │    ESP32-CAM     │
│   R3 (sensör +   │────────▶│  (kamera + WiFi  │
│   keypad + kilit)│         │  + SD + bulut)   │
└─────────────────┘         └────────┬────────┘
                                      │ WiFi / HTTP
                                      ▼
                              ┌──────────────┐
                              │   Supabase    │  (görüntü + sensör verisi + komutlar)
                              └──────┬───────┘
                                      │ Expo Push
                                      ▼
                              ┌──────────────┐
                              │ React Native  │  (mobil uygulama: izleme + uzaktan yönetim)
                              │   / Expo app  │
                              └──────────────┘
```

## Donanım

| Bileşen | Görev |
|---|---|
| Arduino Uno R3 | Ana mikrodenetleyici (sensörler, keypad, kilit) |
| ESP32-CAM | Kamera, WiFi, SD kart, bulut aktarımı |
| DHT11 | Sıcaklık / nem (dinamik şifre kaynağı) |
| HC-SR04 | Ultrasonik mesafe / yaklaşma algılama |
| SW-420 | Titreşim / sabotaj algılama |
| LDR | Ortam ışığı (gece modu) |
| IR alıcı | Kumanda girişi |
| 4×3 Keypad | Şifre girişi |
| Servo motor | Kilit mekanizması |
| Buzzer | Sesli alarm |

### Arduino Uno Pin Bağlantıları

| Pin | Bağlantı |
|---|---|
| D2 | DHT11 |
| D3 | SW-420 titreşim |
| D4 / D5 | HC-SR04 (Trig / Echo) |
| D6 | IR alıcı |
| D7–D12, A3 | 4×3 Keypad |
| A0 | LDR |
| A1 | Servo |
| A2 | Buzzer |

## Teknolojiler

`C++ (Arduino)` · `Arduino Uno R3` · `ESP32-CAM` · `Supabase` · `React Native / Expo` · `Expo Push Notifications`

## Kurulum

### Donanım yazılımı (firmware)

1. Arduino IDE'ye gerekli kütüphaneleri kurun: `DHT`, `Keypad`, `Servo`, `IRremote`, ve ESP32 kart paketi (`esp_camera`, `SD_MMC`, `WiFi`, `HTTPClient`).
2. `sketch_v9.ino` dosyasını **Arduino Uno**'ya yükleyin.
3. `esp32cam_v12.ino` dosyasını **ESP32-CAM**'e yükleyin.

### Yapılandırma (önemli — güvenlik)

ESP32-CAM kodundaki WiFi ve Supabase bilgilerini **doğrudan koda yazmayın**. Aşağıdaki değerleri kendi ortamınıza göre, tercihen ayrı bir `secrets.h` dosyasında tanımlayın ve bu dosyayı `.gitignore`'a ekleyin:

```cpp
// secrets.h  (repoya EKLENMEZ)
#define WIFI_SSID     "WIFI_AGINIZ"
#define WIFI_PASS     "WIFI_SIFRENIZ"
#define SUPABASE_URL  "https://<projeniz>.supabase.co"
#define SUPABASE_KEY  "SUPABASE_ANON_ANAHTARINIZ"
```

> ⚠️ Anahtarlarınızı herkese açık bir depoya göndermeyin. Yanlışlıkla gönderdiyseniz Supabase panelinden anahtarı **yenileyin (rotate)** ve WiFi şifrenizi değiştirin.

## Dosyalar

```
.
├── sketch_v9.ino        # Arduino Uno firmware (sensörler, keypad, kilit, alarm)
├── esp32cam_v12.ino     # ESP32-CAM firmware (kamera, WiFi, SD, Supabase)
└── README.md
```

## Geliştiriciler

- **Yusuf Kasap** — kspyusuf.00@gmail.com · [github.com/kpsyusuf](https://github.com/kpsyusuf)
- **Hasan Hüseyin Yüce**

**Danışman:** Dr. Öğr. Üyesi Tayfun Abut — Muş Alparslan Üniversitesi
