import paho.mqtt.client as mqtt
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS
import json
import logging

# Настройка логирования
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# Конфигурация MQTT
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_TOPIC = "energy/data"
MQTT_USERNAME = "user"
MQTT_PASSWORD = "pass"

# Конфигурация InfluxDB
INFLUX_HOST = "ip"  # Заменить на реальный адрес
INFLUX_PORT = 8086
INFLUX_TOKEN = "token"
INFLUX_ORG = "org"
INFLUX_BUCKET = "iot-data"

# Подключение к InfluxDB
influx_client = InfluxDBClient(url=f"http://{INFLUX_HOST}:{INFLUX_PORT}", token=INFLUX_TOKEN, org=INFLUX_ORG)
write_api = influx_client.write_api(write_options=SYNCHRONOUS)

def on_connect(client, userdata, flags, rc):
    logger.info(f"Connected to MQTT broker with result code {rc}")
    client.subscribe(MQTT_TOPIC)

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
        logger.info(f"Received data: {payload}")
        
        # Создание точки данных для InfluxDB
        point = Point("energy_measurement") \
            .field("power", float(payload["power"])) \
            .field("voltage", float(payload["voltage"])) \
            .field("current", float(payload["current"])) \
            .field("active_energy_total", float(payload["active_energy"]["total"])) \
            .field("active_energy_t1", float(payload["active_energy"]["t1"])) \
            .field("active_energy_t2", float(payload["active_energy"]["t2"])) \
            .field("active_energy_t3", float(payload["active_energy"]["t3"])) \
            .field("active_energy_t4", float(payload["active_energy"]["t4"])) \
            .field("reactive_energy_total", float(payload["reactive_energy"]["total"]))
        
        # Запись данных в InfluxDB
        write_api.write(bucket=INFLUX_BUCKET, record=point)
        logger.info("Data successfully written to InfluxDB")
        
    except Exception as e:
        logger.error(f"Error processing message: {e}")

def main():
    # Настройка MQTT клиента
    client = mqtt.Client()
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        client.loop_forever()
    except KeyboardInterrupt:
        logger.info("Shutting down...")
    except Exception as e:
        logger.error(f"Error: {e}")
    finally:
        client.disconnect()
        influx_client.close()

if __name__ == "__main__":
    main()
