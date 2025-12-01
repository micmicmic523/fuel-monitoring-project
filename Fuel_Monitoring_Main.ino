#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <math.h>

#include <WiFi.h>
#include "RTClib.h"
#include <time.h>

// -------------------- KONFIG UTAMA --------------------
const int MQ135_PIN = 34;
const int ONE_WIRE_BUS = 18;

const float VIN = 5.0;
const float RL = 10.0;
const float V_REF = 3.3;
const float ADC_MAX = 4095.0;

// -------------------- KALIBRASI -----------------------
const float R0_STANDAR = 478.776;
const float M_KOMPENSASI = 0.0336;
const float C_KOMPENSASI = 0.2912;

// -------------------- ACUAN ---------------------------
const float RASIO_MURNI_REF = 0.012241;

// -------------------- MODEL PPM ------------------------
const float A_VAPOR = 100.0;
const float B_VAPOR = -1.4;

// DS18B20
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// -------------------- FILTER ---------------------------
const int ADC_READINGS_PER_SAMPLE = 20;
const int DISCARD_INITIAL_SAMPLES = 3;

const int MEDIAN_WINDOW = 7;
float ppm_history[MEDIAN_WINDOW];
int ppm_hist_idx = 0;
bool ppm_hist_filled = false;

float ppm_ema = -1.0;
const float EMA_ALPHA = 0.25;

// -------------------- RTC / WIFI -----------------------
RTC_DS3231 rtc; // jika pakai DS1307: RTC_DS1307 rtc;

const char* ssid = "TEUS_IOT";
const char* pass = "acdcngalahinsemuanya";

const long gmtOffset_sec = 7 * 3600; // WIB = UTC+7
const int daylightOffset_sec = 0;

const char* ntpServers[] = {"pool.ntp.org", "time.google.com"};

const char* hariID[]  = {"Minggu","Senin","Selasa","Rabu","Kamis","Jumat","Sabtu"};
const char* bulanID[] = {"","Januari","Februari","Maret","April","Mei","Juni",
                         "Juli","Agustus","September","Oktober","November","Desember"};

// -------------------- HELPER ---------------------------
float median_of_window() {
  int filled = ppm_hist_filled ? MEDIAN_WINDOW : ppm_hist_idx;
  if (filled == 0) return 0;

  float tmp[MEDIAN_WINDOW];
  for (int i = 0; i < filled; i++) tmp[i] = ppm_history[i];

  for (int i = 0; i < filled - 1; i++) {
    for (int j = i + 1; j < filled; j++) {
      if (tmp[j] < tmp[i]) {
        float t = tmp[i];
        tmp[i] = tmp[j];
        tmp[j] = t;
      }
    }
  }
  return tmp[filled / 2];
}

// -------------------- HITUNG RASIO ----------------------
float hitung_rs_ro_koreksi(float T_aktual) {
  long adc_sum = 0;
  for (int i = 0; i < ADC_READINGS_PER_SAMPLE; i++) {
    adc_sum += analogRead(MQ135_PIN);
    delay(1);
  }

  float adc_raw = (float)adc_sum / ADC_READINGS_PER_SAMPLE;
  float V_RL = (adc_raw / ADC_MAX) * V_REF;

  // debug kecil ke serial
  Serial.print("DBG adc_raw="); Serial.print(adc_raw, 3);
  Serial.print(" V_RL="); Serial.println(V_RL, 5);

  if (V_RL <= 0 || V_RL >= VIN) return -1;

  float R_S = RL * ((VIN - V_RL) / V_RL);
  float rasio_terukur = R_S / R0_STANDAR;
  float K_T = (M_KOMPENSASI * T_aktual) + C_KOMPENSASI;
  float rasio_koreksi = rasio_terukur * K_T;

  Serial.print("DBG Rs="); Serial.print(R_S, 4);
  Serial.print(" terukur="); Serial.print(rasio_terukur, 6);
  Serial.print(" K_T="); Serial.print(K_T, 6);
  Serial.print(" rasio_koreksi="); Serial.println(rasio_koreksi, 6);

  return rasio_koreksi;
}

// -------------------- SERIAL OUTPUT (pengganti OLED) ----
void tampilkan_serial(float rasio, float deviasi, float ppm, float suhu, String status) {
  Serial.println("========================================");
  Serial.print("Ref Rs/R0: "); Serial.println(RASIO_MURNI_REF, 6);
  Serial.print("Sampel (rasio): "); Serial.println(rasio, 6);
  Serial.print("Suhu (C): "); Serial.println(suhu, 1);
  Serial.print("Status: "); Serial.println(status);
  Serial.print("Deviasi: "); Serial.println(deviasi, 3);
  Serial.print("PPM (rounded): "); Serial.println((long)ppm);
  Serial.println("========================================");
}

// -------------------- SETUP ------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  // I2C pins: SDA=21, SCL=22 (ESP32) - masih diperlukan untuk RTC
  Wire.begin(21, 22);
  Serial.println("\n=== ESP32 Combined: MQ135 + DS18B20 + RTC (NTP sync) ===");

  // Inisialisasi sensor suhu (DS18B20)
  sensors.begin();

  analogReadResolution(12);
  // atur attenuasi agar ADC lebih toleran terhadap tegangan ~3.3V
  analogSetPinAttenuation(MQ135_PIN, ADC_11db);

  // Inisialisasi RTC
  if (!rtc.begin()) {
    Serial.println("Error: RTC tidak terdeteksi. Periksa wiring.");
    // tetap lanjut, karena program masih dapat menampilkan pembacaan sensor
  }

  // Connect WiFi untuk sinkron NTP (opsional)
  Serial.print("Connecting to WiFi ");
  Serial.print(ssid);
  WiFi.begin(ssid, pass);
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) {
    Serial.print(".");
    delay(400);
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Gagal konek WiFi. Akan tampil waktu dari RTC (jika ada).");
    // Jika WiFi gagal, lanjutkan menampilkan waktu RTC saja
  } else {
    Serial.println("WiFi connected.");
    // Konfigurasi timezone & NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServers[0], ntpServers[1]);

    // Tunggu sampai waktu sistem sinkron (timeout ~10s)
    Serial.print("Menunggu sinkron NTP");
    time_t now = time(nullptr);
    int wait = 0;
    while (now < 1600000000UL && wait < 20) { // tanggal acuan di 2020
      Serial.print(".");
      delay(500);
      now = time(nullptr);
      wait++;
    }
    Serial.println();

    if (now < 1600000000UL) {
      Serial.println("Sinkron NTP gagal/takes too long. Lanjut menampilkan RTC lokal.");
    } else {
      // Dapatkan waktu lokal (sudah mengikuti gmtOffset_sec)
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      Serial.print("Waktu NTP (lokal) didapat: ");
      Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
                    timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

      if (rtc.begin()) {
        // Bandingkan dengan RTC
        time_t rtcEpoch;
        {
          DateTime dt = rtc.now();
          struct tm t;
          t.tm_year = dt.year() - 1900;
          t.tm_mon  = dt.month() - 1;
          t.tm_mday = dt.day();
          t.tm_hour = dt.hour();
          t.tm_min  = dt.minute();
          t.tm_sec  = dt.second();
          t.tm_isdst = 0;
          rtcEpoch = mktime(&t);
        }

        time_t ntpEpoch = mktime(&timeinfo); // local epoch
        long diff = (long)difftime(rtcEpoch, ntpEpoch); // rtc - ntp (detik)

        Serial.print("Selisih RTC - NTP (detik): ");
        Serial.println(diff);

        // Aturan: jika RTC kehilangan daya (lostPower) atau selisih > 2 detik -> set RTC
        if (rtc.lostPower() || abs(diff) > 2) {
          Serial.println("Mengatur RTC ke waktu NTP (WIB)...");
          DateTime dt(timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday,
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
          rtc.adjust(dt);
          Serial.println("RTC telah diset.");
        } else {
          Serial.println("RTC sudah cukup akurat. Tidak perlu set ulang.");
        }
      } else {
        Serial.println("RTC tidak tersedia untuk perbandingan/set.");
      }
    }
  }

  Serial.println("Mulai menampilkan waktu realtime dari RTC dan pembacaan sensor...");
  Serial.println();
}

// -------------------- LOOP -------------------------------
unsigned long lastSensorMillis = 0;
const unsigned long sensorInterval = 300; // sesuai delay(300) di program asli

unsigned long lastRtcPrint = 0;
const unsigned long rtcPrintInterval = 1000; // tampilkan waktu RTC tiap detik

static int read_count = 0;

void loop() {
  unsigned long nowMillis = millis();

  // ---------- Bagian sensor & Serial (sekitar tiap 300 ms) ----------
  if (nowMillis - lastSensorMillis >= sensorInterval) {
    lastSensorMillis = nowMillis;

    sensors.requestTemperatures();
    float suhu = sensors.getTempCByIndex(0);

    if (suhu == DEVICE_DISCONNECTED_C) {
      tampilkan_serial(0, 0, 0, 0, "ERROR: DS18B20");
      Serial.println("ERROR: DS18B20 terputus.");
      // tetap lanjut ke RTC bagian
    } else {
      float rasio = hitung_rs_ro_koreksi(suhu);
      if (rasio <= 0) {
        tampilkan_serial(0, 0, 0, suhu, "ERROR: Rasio tidak valid");
        Serial.println("ERROR: Rasio tidak valid.");
      } else {
        float deviasi = fabs(rasio - RASIO_MURNI_REF);

        float ppm_raw = A_VAPOR * pow(rasio, B_VAPOR);
        if (isnan(ppm_raw) || isinf(ppm_raw)) ppm_raw = 0;

        read_count++;
        if (read_count <= DISCARD_INITIAL_SAMPLES) {
          tampilkan_serial(rasio, deviasi, ppm_raw, suhu, "STABILISASI");
          Serial.println("STABILISASI sample (buang awal).");
        } else {
          // update median window
          ppm_history[ppm_hist_idx++] = ppm_raw;
          if (ppm_hist_idx >= MEDIAN_WINDOW) {
            ppm_hist_idx = 0;
            ppm_hist_filled = true;
          }

          float ppm_median = median_of_window();
          if (ppm_ema < 0) ppm_ema = ppm_raw;
          else ppm_ema = EMA_ALPHA * ppm_raw + (1 - EMA_ALPHA) * ppm_ema;

          float ppm = (ppm_median + ppm_ema) / 2.0;

          Serial.print("DEBUG rasio="); Serial.print(rasio, 6);
          Serial.print(" dev="); Serial.print(deviasi, 6);
          Serial.print(" ppm="); Serial.println(ppm);

          // ==============================
          //         FINAL CHECK
          // ==============================
          String status = "TIDAK YAKIN";

          if (deviasi > 0.05) {
            status = "TIDAK BENSIN";
            Serial.println("FINAL: DEV > 0.05 => TIDAK BENSIN");
          } else {
            Serial.println("DEV <= 0.05 => lanjut ke cek PPM");

            if (ppm >= 1000 && ppm <= 10000) {
              status = "MURNI";
              Serial.println("FINAL: PPM 1000-10000 => MURNI");
            } else {
              status = "TIDAK MURNI";
              Serial.println("FINAL: PPM di luar 1000-10000 => TIDAK MURNI");
            }
          }

          tampilkan_serial(rasio, deviasi, ppm, suhu, status);
        }
      }
    }
  }

  // ---------- Bagian RTC: tampilkan waktu RTC ke Serial tiap detik ----------
  if (nowMillis - lastRtcPrint >= rtcPrintInterval) {
    lastRtcPrint = nowMillis;

    if (rtc.begin()) {
      DateTime now = rtc.now(); // baca dari RTC
      uint8_t dow = now.dayOfTheWeek(); // 0 = Minggu
      const char* namaHari = hariID[dow];

      // format hh:mm:ss
      char jam[9];
      sprintf(jam, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

      Serial.print(namaHari);
      Serial.print(", ");
      Serial.print(now.day());
      Serial.print(" ");
      Serial.print(bulanID[now.month()]);
      Serial.print(" ");
      Serial.print(now.year());
      Serial.print(" - ");
      Serial.print(jam);
      Serial.println(" WIB");
    } else {
      Serial.println("RTC tidak tersedia (tidak terdeteksi).");
    }
  }

  // biarkan CPU menangani lainnya
  // tanpa delay besar agar sensor timing tetap akurat
}
