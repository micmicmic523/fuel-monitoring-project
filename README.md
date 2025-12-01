# ⛽ IoT Fuel Purity Monitoring System

An intelligent IoT device capable of detecting fuel purity (Gasoline/Pertamax) based on gas concentration analysis. This project utilizes signal processing algorithms to ensure accurate readings and features real-time clock synchronization via NTP.

> 🏆 **Achievement:** 3rd Place Winner - ElectroChampions National IoT Competition

## 🚀 Key Features
* **Smart Detection Logic:** Classifies fuel status into `MURNI` (Pure), `TIDAK MURNI` (Impure), or `NON-FUEL` based on calculated PPM and Sensor Resistance Ratio ($R_s/R_0$).
* **Signal Processing:** Implements **Median Filter** and **Exponential Moving Average (EMA)** to smooth out sensor noise and provide stable readings.
* **Environmental Compensation:** Automatically adjusts readings based on ambient temperature using DS18B20 sensor.
* **Time Synchronization:** Syncs internal RTC (DS3231) with Internet Time (NTP) whenever WiFi is available, ensuring accurate data logging.

## 🛠️ Hardware Specifications
* **Microcontroller:** ESP32 Development Board
* **Gas Sensor:** MQ-135 (Modified for hydrocarbon sensitivity)
* **Temp Sensor:** DS18B20 (Waterproof)
* **RTC Module:** DS3231 (I2C)
* **Connectivity:** WiFi 2.4GHz

## 🔌 Pin Configuration (Wiring)
| Component | ESP32 Pin |
| :--- | :--- |
| **MQ-135 Analog** | GPIO 34 (ADC1) |
| **DS18B20 Data** | GPIO 18 |
| **I2C SDA (RTC)** | GPIO 21 |
| **I2C SCL (RTC)** | GPIO 22 |

## 🧠 Algorithms & Logic
The firmware processes the analog signal using the following steps:
1.  **Sampling:** Reads 20 samples per cycle.
2.  **Filtering:** Applies Median Filter (Window 7) to remove outliers, followed by EMA (Alpha 0.25) for smoothing.
3.  **Compensation:** Calculates correction factor $K_T$ based on temperature.
4.  **Classification:**
    * *If Deviation > 0.05:* **NOT FUEL**
    * *If PPM 1000 - 10000:* **PURE (MURNI)**
    * *Else:* **IMPURE (TIDAK MURNI)**

## 💻 Tech Stack
* **Language:** C++ (Arduino Framework)
* **Libraries:** `DallasTemperature`, `RTClib`, `WiFi`, `OneWire`

---
*Developed by [Nama Kamu]*
