#include <HardwareSerial.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <math.h>

// ==============================================================================
// --- НАСТРОЙКИ ПОДКЛЮЧЕНИЯ ---
// ==============================================================================

// Пины для MAX485
#define DE_RE_PIN 4  // DE и RE объединены - управление направлением передачи

// Используем Hardware Serial (Serial2)
#define RXD2 16      // RX пин для Serial2
#define TXD2 17      // TX пин для Serial2

// Настройки Wi-Fi
const char* ssid = "ssid";
const char* password = "pass";

// Настройки MQTT
const char* mqtt_server = "ip";  // Адрес MQTT брокера
const int mqtt_port = 1883;                 // Порт MQTT брокера
const char* mqtt_user = "user";    // Замените на ваше имя пользователя
const char* mqtt_password = "pass"; // Замените на ваш пароль
const char* mqtt_topic = "energy/data";     // Топик для отправки данных

WiFiClient espClient;
PubSubClient client(espClient);

// Интервал отправки данных (30 секунд)
const unsigned long sendInterval = 30000;
unsigned long lastSendTime = 0;

// ==============================================================================
// --- ФУНКЦИИ ДЕКОДИРОВАНИЯ ---
// ==============================================================================

bool isBcdValid(uint8_t byte) {
    return ((byte & 0x0F) <= 9) && ((byte >> 4) <= 9);
}

uint8_t bcdToInt(uint8_t byte) {
    if (!isBcdValid(byte)) {
        Serial.printf("Неверный BCD байт: 0x%02X\n", byte);
        return 0;
    }
    return (byte >> 4) * 10 + (byte & 0x0F);
}

void decodeWithOffset(uint8_t* payload, size_t len) {
    for (size_t i = 0; i < len; i++) {
        payload[i] -= 0x33;
    }
}

String decodeValue(uint8_t* payload, size_t len, const char* unit, int divisor_power, int display_places) {
    if (len == 0) return "Ошибка: пустой пакет";
    
    uint8_t decoded[len];
    memcpy(decoded, payload, len);
    decodeWithOffset(decoded, len);
    
    // Little-Endian BCD декодирование
    String valStr = "";
    for (int i = len - 1; i >= 0; i--) {
        uint8_t byte = decoded[i];
        if (!isBcdValid(byte)) {
            return "Ошибка BCD";
        }
        valStr += String((byte >> 4) & 0x0F, DEC);
        valStr += String(byte & 0x0F, DEC);
    }
    
    // Преобразуем в число и делим на делитель
    long value = 0;
    for (size_t i = 0; i < valStr.length(); i++) {
        value = value * 10 + (valStr[i] - '0');
    }
    
    float floatValue = (float)value / pow(10, divisor_power);
    
    char result[50];
    if (display_places == 0) {
        sprintf(result, "%d %s", (int)floatValue, unit);
    } else {
        char format[10];
        sprintf(format, "%%.%df %%s", display_places);
        sprintf(result, format, floatValue, unit);
    }
    
    return String(result);
}

// ==============================================================================
// --- ФУНКЦИИ ДЛЯ РАЗБОРА КОНКРЕТНЫХ ДАННЫХ ---
// ==============================================================================

String parseActiveEnergyTariffs(uint8_t* response, size_t len) {
    if (len < 35) return "Ошибка: неполный пакет ответа";
    
    String result = "\n";
    result += "  Всего: " + decodeValue(response + 15, 4, "кВтч", 2, 2) + "\n";
    result += "  T1:    " + decodeValue(response + 19, 4, "кВтч", 2, 2) + "\n";
    result += "  T2:    " + decodeValue(response + 23, 4, "кВтч", 2, 2) + "\n";
    result += "  T3:    " + decodeValue(response + 27, 4, "кВтч", 2, 2) + "\n";
    result += "  T4:    " + decodeValue(response + 31, 4, "кВтч", 2, 2);
    
    return result;
}

String parseReactiveEnergyTotal(uint8_t* response, size_t len) {
    if (len < 19) return "Ошибка: неполный пакет ответа";
    return decodeValue(response + 15, 4, "кварч", 2, 2);
}

String parsePower(uint8_t* response, size_t len) {
    if (len < 18) return "Ошибка: неполный пакет ответа";
    return decodeValue(response + 15, 3, "кВт", 4, 4);
}

String parseVoltage(uint8_t* response, size_t len) {
    if (len < 17) return "Ошибка: неполный пакет ответа";
    return decodeValue(response + 15, 2, "В", 0, 2);
}

String parseCurrent(uint8_t* response, size_t len) {
    if (len < 17) return "Ошибка: неполный пакет ответа";
    return decodeValue(response + 15, 2, "А", 2, 2);
}

// ==============================================================================
// --- КОМАНДЫ ---
// ==============================================================================

struct Command {
    const char* description;
    const char* request;
    String (*parser)(uint8_t*, size_t);
};

Command commands[] = {
    {"Запросить Мгновенную мощность", "FEFEFE6818478400000068010263E90216", parsePower},
    {"Запросить Напряжение (Фаза А)", "FEFEFE6818478400000068010244E9E316", parseVoltage},
    {"Запросить Ток (Фаза А)", "FEFEFE6818478400000068010254E9F316", parseCurrent},
    {"Запросить Активную энергию (Всего + Тарифы)", "FEFEFE6818478400000068010252C3CB16", parseActiveEnergyTariffs},
    {"Запросить Реактивную энергию (Всего)", "FEFEFE6818478400000068010243C4BD16", parseReactiveEnergyTotal}
};

const int commandCount = sizeof(commands) / sizeof(commands[0]);

// ==============================================================================
// --- ФУНКЦИИ СВЯЗИ ---
// ==============================================================================

void enableTransmission() {
    digitalWrite(DE_RE_PIN, HIGH);  // HIGH = передача
    delay(1);
}

void enableReception() {
    digitalWrite(DE_RE_PIN, LOW);   // LOW = прием
    delay(1);
}

size_t hexStringToBytes(const char* hexString, uint8_t* bytes, size_t maxBytes) {
    size_t len = strlen(hexString);
    size_t byteCount = 0;
    
    for (size_t i = 0; i < len && byteCount < maxBytes; i += 2) {
        if (i + 1 < len) {
            char hex[3] = {hexString[i], hexString[i + 1], '\0'};
            bytes[byteCount++] = (uint8_t)strtol(hex, NULL, 16);
        }
    }
    
    return byteCount;
}

void printHex(uint8_t* data, size_t len, const char* prefix) {
    Serial.print(prefix);
    for (size_t i = 0; i < len; i++) {
        Serial.printf("%02X", data[i]);
        if (i < len - 1) Serial.print(" ");
    }
    Serial.println();
}

bool sendRequest(const char* hexRequest, uint8_t* response, size_t maxResponseLen, size_t* responseLen) {
    uint8_t request[50];
    size_t requestLen = hexStringToBytes(hexRequest, request, sizeof(request));
    
    if (requestLen == 0) {
        Serial.println("Ошибка: неверная hex строка");
        return false;
    }
    
    // Очищаем буферы
    Serial2.flush();
    while (Serial2.available()) Serial2.read();
    
    // Отправляем запрос
    enableTransmission();
    printHex(request, requestLen, "-> Отправка (TX): ");
    Serial2.write(request, requestLen);
    Serial2.flush();
    
    // Переключаемся на прием
    enableReception();
    
    // Ожидаем ответ
    unsigned long startTime = millis();
    size_t bytesReceived = 0;
    
    while (millis() - startTime < 3000 && bytesReceived < maxResponseLen) {
        if (Serial2.available()) {
            response[bytesReceived++] = Serial2.read();
            startTime = millis(); // обновляем время при получении данных
        }
        delay(10);
    }
    
    *responseLen = bytesReceived;
    
    if (bytesReceived > 0) {
        printHex(response, bytesReceived, "<- Получен ответ (RX): ");
        return true;
    } else {
        Serial.println("<- Ответ от устройства не получен (таймаут)");
        return false;
    }
}

// ==============================================================================
// --- ФУНКЦИИ ДЛЯ РАБОТЫ С WI-FI И MQTT ---
// ==============================================================================

void connectToWiFi() {
  Serial.print("Подключение к Wi-Fi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nПодключено к Wi-Fi!");
    Serial.print("IP адрес: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nНе удалось подключиться к Wi-Fi!");
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Попытка подключения к MQTT...");
    
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("подключено");
    } else {
      Serial.print("ошибка, rc=");
      Serial.print(client.state());
      Serial.println(" пробую снова через 5 секунд");
      delay(5000);
    }
  }
}

// Функция для извлечения числового значения из строки с единицами измерения
float extractValue(String dataString) {
  // Удаляем пробелы в начале и конце
  dataString.trim();
  
  // Ищем первый пробел (разделитель между числом и единицей измерения)
  int spaceIndex = dataString.indexOf(' ');
  if (spaceIndex == -1) {
    // Если пробела нет, пробуем преобразовать всю строку
    return dataString.toFloat();
  }
  
  // Извлекаем числовую часть
  String valueStr = dataString.substring(0, spaceIndex);
  return valueStr.toFloat();
}

// Функция для извлечения значений активной энергии из строки с тарифами
void parseActiveEnergyValues(String energyStr, float& total, float& t1, float& t2, float& t3, float& t4) {
  total = t1 = t2 = t3 = t4 = 0.0;
  
  // Ищем позиции каждого тарифа
  int totalPos = energyStr.indexOf("Всего:");
  int t1Pos = energyStr.indexOf("T1:");
  int t2Pos = energyStr.indexOf("T2:");
  int t3Pos = energyStr.indexOf("T3:");
  int t4Pos = energyStr.indexOf("T4:");
  
  if (totalPos != -1 && t1Pos != -1) {
    // Извлекаем значение для "Всего"
    // int totalEnd = energyStr.indexOf("кВтч", totalPos);
    // if (totalEnd != -1) {
    //   String totalStr = energyStr.substring(totalPos + 7, totalEnd);
    //   totalStr.trim();
    //   total = extractValue(totalStr);
    // }
    
    // Извлекаем значение для T1
    int t1End = energyStr.indexOf("кВтч", t1Pos);
    if (t1End != -1) {
      String t1Str = energyStr.substring(t1Pos + 4, t1End);
      t1Str.trim();
      total = t1 = extractValue(t1Str);
    }
    
    // Извлекаем значение для T2
    int t2End = energyStr.indexOf("кВтч", t2Pos);
    if (t2End != -1) {
      String t2Str = energyStr.substring(t2Pos + 4, t2End);
      t2Str.trim();
      t2 = extractValue(t2Str);
    }
    
    // Извлекаем значение для T3
    int t3End = energyStr.indexOf("кВтч", t3Pos);
    if (t3End != -1) {
      String t3Str = energyStr.substring(t3Pos + 4, t3End);
      t3Str.trim();
      t3 = extractValue(t3Str);
    }
    
    // Извлекаем значение для T4
    int t4End = energyStr.indexOf("кВтч", t4Pos);
    if (t4End != -1) {
      String t4Str = energyStr.substring(t4Pos + 4, t4End);
      t4Str.trim();
      t4 = extractValue(t4Str);
    }
  }
}

bool sendDataViaMQTT(float power, float voltage, float current, 
                    float activeTotal, float activeT1, float activeT2, float activeT3, float activeT4,
                    float reactiveTotal) {
  if (!client.connected()) {
    reconnectMQTT();
  }
  
  // Формируем JSON для отправки
  String jsonData = "{";
  jsonData += "\"power\":" + String(power, 4) + ",";
  jsonData += "\"voltage\":" + String(voltage, 2) + ",";
  jsonData += "\"current\":" + String(current, 2) + ",";
  jsonData += "\"active_energy\": {";
  jsonData += "\"total\":" + String(activeTotal, 2) + ",";
  jsonData += "\"t1\":" + String(activeT1, 2) + ",";
  jsonData += "\"t2\":" + String(activeT2, 2) + ",";
  jsonData += "\"t3\":" + String(activeT3, 2) + ",";
  jsonData += "\"t4\":" + String(activeT4, 2);
  jsonData += "},";
  jsonData += "\"reactive_energy\": {";
  jsonData += "\"total\":" + String(reactiveTotal, 2);
  jsonData += "}";
  jsonData += "}";
  
  Serial.println("Отправка данных через MQTT: " + jsonData);
  
  return client.publish(mqtt_topic, jsonData.c_str());
}

// ==============================================================================
// --- ОСНОВНАЯ ПРОГРАММА ---
// ==============================================================================

void setup() {
  Serial.begin(115200);
  Serial2.begin(2400, SERIAL_8E1, RXD2, TXD2);
  
  pinMode(DE_RE_PIN, OUTPUT);
  enableReception();
  
  // Подключаемся к Wi-Fi
  connectToWiFi();
  
  // Настраиваем MQTT
  client.setServer(mqtt_server, mqtt_port);
  
  Serial.println("\n=== ESP32 Счетчик с MAX485 и MQTT ===");
  Serial.println("Инициализация завершена");
  
  delay(2000);
}

void loop() {
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  
  // Проверяем, прошло ли 30 секунд с последней отправки
  if (millis() - lastSendTime >= sendInterval) {
    Serial.println("\n--- Начало опроса счетчика ---");
    
    String powerStr, voltageStr, currentStr, activeEnergyStr, reactiveEnergyStr;
    float power = 0, voltage = 0, current = 0, reactiveTotal = 0;
    float activeTotal = 0, activeT1 = 0, activeT2 = 0, activeT3 = 0, activeT4 = 0;
    
    // Запрашиваем все данные по очереди
    for (int i = 0; i < commandCount; i++) {
      Serial.println("\nКоманда: " + String(commands[i].description));
      
      uint8_t response[100];
      size_t responseLen;
      
      if (sendRequest(commands[i].request, response, sizeof(response), &responseLen)) {
        String result = commands[i].parser(response, responseLen);
        Serial.println("Результат: " + result);
        
        // Сохраняем результаты для отправки на сервер
        switch(i) {
          case 0: 
            powerStr = result;
            power = extractValue(powerStr);
            break;
          case 1:
            voltageStr = result;
            voltage = extractValue(voltageStr);
            break;
          case 2:
            currentStr = result;
            current = extractValue(currentStr);
            break;
          case 3:
            activeEnergyStr = result;
            parseActiveEnergyValues(activeEnergyStr, activeTotal, activeT1, activeT2, activeT3, activeT4);
            break;
          case 4:
            reactiveEnergyStr = result;
            reactiveTotal = extractValue(reactiveEnergyStr);
            break;
        }
      }
      
      delay(1000); // Задержка между запросами
    }
    
    // Отправляем данные через MQTT
    if (sendDataViaMQTT(power, voltage, current, 
                        activeTotal, activeT1, activeT2, activeT3, activeT4,
                        reactiveTotal)) {
      Serial.println("Данные успешно отправлены через MQTT");
    } else {
      Serial.println("Ошибка отправки данных через MQTT");
    }
    
    lastSendTime = millis();
    Serial.println("--- Опрос завершен. Ожидание следующего цикла ---");
  }
  
  // Проверяем подключение к Wi-Fi каждые 10 секунд
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck >= 10000) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Потеряно подключение к Wi-Fi. Переподключаемся...");
      connectToWiFi();
    }
    lastWifiCheck = millis();
  }
  
  delay(1000);
}