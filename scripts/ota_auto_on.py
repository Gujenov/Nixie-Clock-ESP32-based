Import("env")

import time


def _send_ota_on(source, target, env=None, **kwargs):
    env_ = env
    if env_ is None:
        return
    # Выполняем только для OTA upload
    upload_protocol = str(env_.GetProjectOption("upload_protocol", "")).strip().lower()
    if upload_protocol != "espota":
        return

    control_port = env_.GetProjectOption("monitor_port", "").strip()
    if not control_port:
        print("[OTA-AUTO] monitor_port не задан, пропускаю auto 'ota on'")
        return

    baud = int(env_.GetProjectOption("monitor_speed", "115200"))

    try:
        import serial  # pyserial из pio virtualenv
    except Exception as exc:
        print(f"[OTA-AUTO] pyserial недоступен: {exc}")
        return

    try:
        print(f"[OTA-AUTO] Отправляю 'ota on' в {control_port} @ {baud}")
        with serial.Serial(control_port, baudrate=baud, timeout=1, write_timeout=1) as ser:
            # Небольшая пауза на открытие CDC порта
            time.sleep(0.6)
            ser.write(b"\n")
            ser.write(b"ota on\n")
            ser.flush()
            time.sleep(1.2)
    except Exception as exc:
        print(f"[OTA-AUTO] Не удалось отправить 'ota on': {exc}")


env.AddPreAction("upload", _send_ota_on)
