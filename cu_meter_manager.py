import serial
import time
import sys

PORT = 'COM5'  # Замените на ваш COM-порт, например 'COM3' или '/dev/ttyUSB0'

# ==============================================================================
# --- 1. ФУНКЦИИ-ДЕКОДЕРЫ (ПАРСЕРЫ) ---
# ==============================================================================

def bcd_to_int(byte):
    """Преобразует один байт из BCD, вызывая ошибку, если формат нарушен."""
    if (byte & 0x0F) > 9 or (byte >> 4) > 9:
        raise ValueError(f"Неверный BCD байт: {hex(byte)}")
    return (byte >> 4) * 10 + (byte & 0x0F)

def decode_with_offset(payload_bytes):
    """Ключевая функция: вычитает 0x33 из каждого байта."""
    return bytes([b - 0x33 for b in payload_bytes])

def decode_value(payload_bytes, unit, divisor_power, display_places):
    """
    Универсальный декодер для числовых значений. (ИСПРАВЛЕНО)
    divisor_power: степень 10 для делителя (0 для Вольт, 2 для Ампер и кВтч)
    display_places: сколько знаков после запятой показывать
    """
    try:
        decoded_payload = decode_with_offset(payload_bytes)
        reversed_bytes = decoded_payload[::-1]
        val_str = "".join([f"{bcd_to_int(b):02d}" for b in reversed_bytes])
        divisor = 10**divisor_power
        float_val = float(val_str) / divisor
        return f"{float_val:.{display_places}f} {unit}"
    except Exception as e:
        return f"Ошибка разбора значения: {e}. HEX данных: {payload_bytes.hex(' ').upper()}"

def parse_datetime(response_bytes):
    """Декодирует ответ с датой и временем."""
    if not response_bytes or len(response_bytes) < 22: return "Ошибка: неполный пакет ответа."
    try:
        payload = response_bytes[15:22]
        decoded_payload = decode_with_offset(payload)
        second, minute, hour = bcd_to_int(decoded_payload[0]), bcd_to_int(decoded_payload[1]), bcd_to_int(decoded_payload[2])
        day, month, year = bcd_to_int(decoded_payload[4]), bcd_to_int(decoded_payload[5]), 2000 + bcd_to_int(decoded_payload[6])
        return f"{day:02d}.{month:02d}.{year} {hour:02d}:{minute:02d}:{second:02d}"
    except Exception as e:
        return f"Ошибка разбора пакета времени: {e}. HEX данных: {response_bytes[15:22].hex(' ').upper()}"

def parse_active_energy_tariffs(response_bytes):
    """Декодирует ответ со значениями активной энергии по всем тарифам."""
    if not response_bytes or len(response_bytes) < 35: return "Ошибка: неполный пакет ответа."
    payload = response_bytes[15:35] 
    results = [
        f"  Всего: {decode_value(payload[0:4], 'кВтч', 2, 2)}",
        f"  T1:    {decode_value(payload[4:8], 'кВтч', 2, 2)}",
        f"  T2:    {decode_value(payload[8:12], 'кВтч', 2, 2)}",
        f"  T3:    {decode_value(payload[12:16], 'кВтч', 2, 2)}",
        f"  T4:    {decode_value(payload[16:20], 'кВтч', 2, 2)}",
    ]
    return "\n" + "\n".join(results)

def parse_reactive_energy_total(response_bytes):
    """Декодирует ответ с суммарной реактивной энергией."""
    if not response_bytes or len(response_bytes) < 19: return "Ошибка: неполный пакет ответа."
    payload = response_bytes[15:19]
    return decode_value(payload, "кварч", 2, 2)

def parse_power(response_bytes):
    """Декодирует ответ с мгновенной мощностью."""
    if not response_bytes or len(response_bytes) < 18: return "Ошибка: неполный пакет ответа."
    payload = response_bytes[15:18]
    return decode_value(payload, "кВт", 4, 4)

def parse_voltage(response_bytes): # <--- ИСПРАВЛЕН ВЫЗОВ
    """Декодирует ответ с напряжением."""
    if not response_bytes or len(response_bytes) < 17: return "Ошибка: неполный пакет ответа."
    payload = response_bytes[15:17]
    # Делим на 1 (10**0), но отображаем с 2 знаками после запятой
    return decode_value(payload, "В", 0, 2)

def parse_current(response_bytes): # <--- ИСПРАВЛЕН ВЫЗОВ
    """Декодирует ответ с током."""
    if not response_bytes or len(response_bytes) < 17: return "Ошибка: неполный пакет ответа."
    payload = response_bytes[15:17]
    # Делим на 100 (10**2) и отображаем с 2 знаками
    return decode_value(payload, "А", 2, 2)

def parse_generic(response_bytes):
    if not response_bytes: return "Ответ не получен."
    return f"Получен необработанный ответ ({len(response_bytes)} байт)."

# ==============================================================================
# --- 2. СЛОВАРЬ КОМАНД (без изменений) ---
# ==============================================================================
COMMANDS = {
    '1': {"description": "Запросить дату и время", "request": "FEFEFE6818478400000068010245F3EE16", "parser": parse_datetime},
    '2': {"description": "Запросить Мгновенную мощность", "request": "FEFEFE6818478400000068010263E90216", "parser": parse_power},
    '3': {"description": "Запросить Напряжение (Фаза А)", "request": "FEFEFE6818478400000068010244E9E316", "parser": parse_voltage},
    '4': {"description": "Запросить Ток (Фаза А)", "request": "FEFEFE6818478400000068010254E9F316", "parser": parse_current},
    '5': {"description": "Запросить Активную энергию (Всего + Тарифы)", "request": "FEFEFE6818478400000068010252C3CB16", "parser": parse_active_energy_tariffs},
    '6': {"description": "Запросить Реактивную энергию (Всего)", "request": "FEFEFE6818478400000068010243C4BD16", "parser": parse_reactive_energy_total},
    'energy': {"description": "ПОЛНЫЙ ОПРОС: Вся ЭНЕРГИЯ (Актив + Реактив)"},
    'chars':  {"description": "ПОЛНЫЙ ОПРОС: Все ХАРАКТЕРИСТИКИ (U, I, P)"},
}

# ==============================================================================
# --- 3. КЛАСС И ОСНОВНАЯ ЛОГИКА (без изменений) ---
# ==============================================================================
class MeterReader:
    def __init__(self, port, baudrate=2400, timeout=3): self.port_name, self.baudrate, self.timeout, self.ser = port, baudrate, timeout, None
    def connect(self):
        try:
            print(f"Попытка подключения к порту {self.port_name} со скоростью {self.baudrate}...")
            self.ser = serial.Serial(port=self.port_name, baudrate=self.baudrate, parity=serial.PARITY_EVEN, stopbits=serial.STOPBITS_ONE, bytesize=serial.EIGHTBITS, timeout=self.timeout)
            self.ser.dtr, self.ser.rts = True, True
            print(f"Порт {self.ser.name} успешно открыт.")
            return True
        except serial.SerialException as e:
            print(f"ОШИБКА: Не удалось открыть порт {self.port_name}.\nПодробности: {e}"); return False
    def disconnect(self):
        if self.ser and self.ser.is_open: self.ser.close(); print("Порт успешно закрыт.")
    def send_request(self, hex_request):
        if not (self.ser and self.ser.is_open): print("Ошибка: порт не открыт."); return None
        try:
            bytes_to_send = bytes.fromhex(hex_request)
            print(f"-> Отправка (TX): {bytes_to_send.hex(' ').upper()}")
            self.ser.reset_input_buffer(); self.ser.reset_output_buffer()
            self.ser.write(bytes_to_send)
            time.sleep(0.5)
            response_bytes = self.ser.read(100)
            if response_bytes: print(f"<- Получен ответ (RX): {response_bytes.hex(' ').upper()}"); return response_bytes
            else: print("<- Ответ от устройства не получен (таймаут)."); return None
        except Exception as e: print(f"Произошла ошибка при обмене данными: {e}"); return None

def main():
    meter = MeterReader(port=PORT)
    if not meter.connect(): sys.exit(1)
    while True:
        print("\n--- Меню команд ---")
        for key, value in COMMANDS.items(): print(f"  {key}: {value['description']}")
        print("  q: Выход"); print("-------------------")
        choice = input("Введите номер команды: ").lower()
        if choice == 'q': break
        if choice == 'energy':
            print("\nВыполняется опрос ЭНЕРГИИ...")
            print("\nШаг 1: Запрос Активной энергии (Тарифы)")
            response1 = meter.send_request(COMMANDS['5']['request'])
            if response1: print(f"Расшифровка:{COMMANDS['5']['parser'](response1)}")
            print("\nШаг 2: Запрос Реактивной энергии (Всего)")
            response2 = meter.send_request(COMMANDS['6']['request'])
            if response2: print(f"Расшифровка: {COMMANDS['6']['parser'](response2)}")
            print("\n--- Опрос ЭНЕРГИИ завершен ---")
        elif choice == 'chars':
            print("\nВыполняется опрос ХАРАКТЕРИСТИК СЕТИ...")
            print("\nШаг 1: Запрос Напряжения")
            response1 = meter.send_request(COMMANDS['3']['request'])
            if response1: print(f"Расшифровка:{COMMANDS['3']['parser'](response1)}")
            print("\nШаг 2: Запрос Тока")
            response2 = meter.send_request(COMMANDS['4']['request'])
            if response2: print(f"Расшифровка: {COMMANDS['4']['parser'](response2)}")
            print("\nШаг 3: Запрос Мгновенной мощности")
            response3 = meter.send_request(COMMANDS['2']['request'])
            if response3: print(f"Расшифровка: {COMMANDS['2']['parser'](response3)}")
            print("\n--- Опрос ХАРАКТЕРИСТИК завершен ---")
        elif choice in COMMANDS:
            command = COMMANDS[choice]
            print(f"\nВыбрана команда: '{command['description']}'")
            response = meter.send_request(command['request'])
            if response:
                parser = command.get('parser', parse_generic)
                print(f"Расшифровка: {parser(response)}")
            print("-" * 20)
        else: print("Неверный выбор. Попробуйте еще раз.")
    meter.disconnect(); print("Программа завершена.")

if __name__ == "__main__":
    main()