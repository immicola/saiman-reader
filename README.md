# Система мониторинга электроэнергии (Energy Monitoring System)

[English version below](#energy-monitoring-system)

## Система мониторинга электроэнергии

Система для считывания данных со счётчиков электроэнергии линейки Орман СО-Э711 код CU, их обработки и визуализации. Проект состоит из трёх основных компонентов:

### Компоненты системы

1. **Менеджер счётчика (cu_meter_manager.py)**
   - Прямое взаимодействие со счётчиком через RS-485
   - Чтение параметров: напряжение, ток, мощность, активная и реактивная энергия
   - Поддержка многотарифного учёта (T1-T4)
   - Интерактивное меню для ручного опроса счётчика

2. **ESP32 MQTT клиент (esp32-meter-mqtt.ino)**
   - Автоматический сбор данных со счётчика через RS-485
   - Отправка данных на MQTT брокер
   - Поддержка Wi-Fi подключения
   - Периодический опрос каждые 30 секунд
   - Автоматическое переподключение при потере связи

3. **Сервер обработки данных (rpi_meter_server.py)**
   - Приём данных через MQTT
   - Сохранение в базу данных InfluxDB
   - Логирование всех операций
   - Обработка ошибок и автоматическое восстановление

### Требования

- Python 3.6+
- ESP32 с поддержкой Wi-Fi
- Преобразователь MAX485 для ESP32 (подключение к счётчику)
- USB-to-RS485 преобразователь для прямого подключения к компьютеру (для cu_meter_manager.py)
- MQTT брокер
- InfluxDB сервер

### Схема подключения

1. **Подключение ESP32 через MAX485:**
   - DE и RE пин MAX485 -> GPIO4 ESP32
   - RO (Receive Output) MAX485 -> GPIO16 (RX2) ESP32
   - DI (Data Input) MAX485 -> GPIO17 (TX2) ESP32
   - VCC MAX485 -> 3.3V или 5V ESP32
   - GND MAX485 -> GND ESP32
   - A и B клеммы MAX485 -> соответствующие клеммы счётчика

2. **Подключение через USB-to-RS485:**
   - Подключите USB-to-RS485 преобразователь к USB порту компьютера
   - A и B клеммы преобразователя -> соответствующие клеммы счётчика
   - В Windows преобразователь появится как COM-порт

### Настройка

1. **Менеджер счётчика:**
   ```python
   PORT = 'COM5'  # Измените на ваш COM-порт
   ```

2. **ESP32:**
   ```cpp
   const char* ssid = "ssid";        // Имя Wi-Fi сети
   const char* password = "pass";     // Пароль Wi-Fi
   const char* mqtt_server = "ip";    // IP адрес MQTT брокера
   const char* mqtt_user = "user";    // Имя пользователя MQTT
   const char* mqtt_password = "pass"; // Пароль MQTT
   ```

3. **Сервер:**
   ```python
   MQTT_BROKER = "localhost"
   INFLUX_HOST = "ip"
   INFLUX_TOKEN = "token"
   INFLUX_ORG = "org"
   INFLUX_BUCKET = "iot-data"
   ```

### Использование

1. Запустите менеджер счётчика для прямого взаимодействия:
   ```bash
   python cu_meter_manager.py
   ```

2. Загрузите прошивку на ESP32 для автоматического сбора данных

3. Запустите сервер для обработки данных:
   ```bash
   python rpi_meter_server.py
   ```

---

## Energy Monitoring System

A system for reading data from Saiman "Орман СО-Э711", code CU electricity meters, processing, and visualization. The project consists of three main components:

### System Components

1. **Meter Manager (cu_meter_manager.py)**
   - Direct interaction with the meter via RS-485
   - Reading parameters: voltage, current, power, active and reactive energy
   - Multi-tariff support (T1-T4)
   - Interactive menu for manual meter polling

2. **ESP32 MQTT Client (esp32-meter-mqtt.ino)**
   - Automatic data collection from the meter via RS-485
   - Sending data to MQTT broker
   - Wi-Fi connectivity support
   - Periodic polling every 30 seconds
   - Automatic reconnection on connection loss

3. **Data Processing Server (rpi_meter_server.py)**
   - Receiving data via MQTT
   - Storing in InfluxDB database
   - Logging all operations
   - Error handling and automatic recovery

### Requirements

- Python 3.6+
- ESP32 with Wi-Fi support
- MAX485 converter for ESP32 (meter connection)
- USB-to-RS485 converter for direct computer connection (for cu_meter_manager.py)
- MQTT broker
- InfluxDB server

### Connection Diagram

1. **ESP32 connection via MAX485:**
   - DE and RE pin MAX485 -> GPIO4 ESP32
   - RO (Receive Output) MAX485 -> GPIO16 (RX2) ESP32
   - DI (Data Input) MAX485 -> GPIO17 (TX2) ESP32
   - VCC MAX485 -> 3.3V or 5V ESP32
   - GND MAX485 -> GND ESP32
   - A and B terminals MAX485 -> corresponding meter terminals

2. **Connection via USB-to-RS485:**
   - Connect USB-to-RS485 converter to computer's USB port
   - A and B terminals of converter -> corresponding meter terminals
   - In Windows, the converter will appear as a COM port

### Setup

1. **Meter Manager:**
   ```python
   PORT = 'COM5'  # Change to your COM port
   ```

2. **ESP32:**
   ```cpp
   const char* ssid = "ssid";        // Wi-Fi network name
   const char* password = "pass";     // Wi-Fi password
   const char* mqtt_server = "ip";    // MQTT broker IP address
   const char* mqtt_user = "user";    // MQTT username
   const char* mqtt_password = "pass"; // MQTT password
   ```

3. **Server:**
   ```python
   MQTT_BROKER = "localhost"
   INFLUX_HOST = "ip"
   INFLUX_TOKEN = "token"
   INFLUX_ORG = "org"
   INFLUX_BUCKET = "iot-data"
   ```

### Usage

1. Run the meter manager for direct interaction:
   ```bash
   python cu_meter_manager.py
   ```

2. Upload the firmware to ESP32 for automatic data collection

3. Start the server for data processing:
   ```bash
   python rpi_meter_server.py
   ```
