import sqlite3
import json
import time
import threading
import requests
import paho.mqtt.client as mqtt

# ——— CONFIGURACIÓN —————————————————————————————————————————————
DB_PATH        = 'incendios.db'
TABLE_NAME     = 'mediciones'
BROKER_LOCAL   = 'localhost'
BROKER_PORT    = 1883
UBIDOTS_DEVICE_LABEL = "raspberry_gateway"
UBIDOTS_TOKEN  = 'BBUS-eh3GhwSHxcKQ8gUpFcNm5uw2BlNPL5'  # Tu token de Ubidots
UBIDOTS_TOPIC  = '/v1.6/devices/raspberry_gateway'

#--- CONSULTAS A VARIABLES DEL DASHBOARD —————————————————————————————————————————————
LED_VAR = "led"
ALARMA_VAR = "alarma"
UBIDOTS_BASE = f"https://industrial.api.ubidots.com/api/v1.6/devices/{UBIDOTS_DEVICE_LABEL}"
HEADERS = {"X-Auth-Token": UBIDOTS_TOKEN}


# Semáforo para evitar conflictos de concurrencia
db_sem = threading.Semaphore(1)

# ——— MQTT CALLBACKS —————————————————————————————————————————————

def on_connect(client, userdata, flags, rc):
    print(f"[MQTT] Conectado al broker local (rc={rc})")
    client.subscribe("fire/data")

def on_message(client, userdata, msg):
    """Guarda en la base y luego reenvía el dato desde la DB a Ubidots"""
    try:
        payload = json.loads(msg.payload.decode())
        timestamp = int(time.time())
        temperatura = payload.get("temperatura")
        gas = payload.get("gas")
        estado = payload.get("estado")
        llama = payload.get("llama")
    except Exception as e:
        print(f"[Error] Payload inválido: {e}")
        return

    # Insertar en la base de datos
    db_sem.acquire()
    try:
        conn = userdata['db_conn']
        cur  = conn.cursor()
        cur.execute(f'''
            INSERT INTO {TABLE_NAME} (timestamp, temperatura, gas, llama, estado)
            VALUES (?, ?, ?, ?, ?)
        ''', (timestamp, temperatura, gas, llama, estado))
        conn.commit()
        print(f"[DB] Guardado registro #{cur.lastrowid}: {payload}")
    finally:
        db_sem.release()

    # Leer el último dato guardado y enviarlo a Ubidots
    db_sem.acquire()
    try:
        conn = sqlite3.connect(DB_PATH)
        cur = conn.cursor()
        cur.execute(f'''
            SELECT timestamp, temperatura, gas, llama, estado
            FROM {TABLE_NAME}
            ORDER BY id DESC
            LIMIT 1
        ''')
        row = cur.fetchone()
        conn.close()
    finally:
        db_sem.release()

    if row:
        ts, temp, gas, llama, estado = row
        data = {
            "temperatura": temp,
            "gas": gas,
            "timestamp": ts,
            "llama": llama,
            "estado": estado,
            "incendio": 1.0 if "INCENDIO" in estado else 0.0
        }

        try:
            ub = mqtt.Client()
            ub.username_pw_set(UBIDOTS_TOKEN)
            ub.connect("industrial.api.ubidots.com", 1883, 60)
            ub.publish(UBIDOTS_TOPIC, json.dumps(data))
            ub.disconnect()
            print(f"[Ubidots] Enviado desde la DB: {data}")
        except Exception as e:
            print(f"[Error] Fallo al enviar a Ubidots: {e}")
    else:
        print("[Ubidots] No hay registros aún en la DB.")

def get_variable_state(variable):
    url = f"{UBIDOTS_BASE}/{variable}/lv"
    try:
        response = requests.get(url, headers=HEADERS, timeout=5)
        if response.status_code == 200:
            return float(response.text.strip())
        else:
            print(f"[Ubidots] Error consultando '{variable}': {response.status_code}")
            return None
    except Exception as e:
        print(f"[Ubidots] Error al consultar '{variable}': {e}")
        return None

def control_loop():
    print("Haciendo consultas a ubistots")
    client_control = mqtt.Client()
    client_control.connect(BROKER_LOCAL, BROKER_PORT, 60)
    client_control.loop_start()

    while True:
        estado_led = get_variable_state(LED_VAR)
        estado_alarma = get_variable_state(ALARMA_VAR)

        if estado_led is not None:
            estado_str = "ON" if estado_led == 1.0 else "OFF"
            client_control.publish("esp32/led", estado_str)
            print(f"[Control] LED => {estado_led}")

        if estado_alarma is not None:
            estado_str = "ON" if estado_alarma == 1.0 else "OFF"
            client_control.publish("esp32/alarma", estado_str)
            print(f"[Control] ALARMA => {estado_alarma}")

        time.sleep(1)  # Espera entre consultas

# ——— PROGRAMA PRINCIPAL ——————————————————————————————————————

def main():
    # Crear conexión SQLite y pasarla como userdata a MQTT
    conn = sqlite3.connect(DB_PATH, check_same_thread=False)

    client = mqtt.Client(userdata={'db_conn': conn})
    client.on_connect = on_connect
    client.on_message = on_message
     # Lanzar hilo de control para escuchar variables de Ubidots
    control_thread = threading.Thread(target=control_loop, daemon=True)
    control_thread.start()

    client.connect(BROKER_LOCAL, BROKER_PORT, 60)
    client.loop_forever()


# ——— EJECUTAR ————————————————————————————————————————————

if __name__ == '__main__':
    main()