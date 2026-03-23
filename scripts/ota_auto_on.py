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
        from serial.tools import list_ports
    except Exception as exc:
        print(f"[OTA-AUTO] pyserial недоступен: {exc}")
        return

    # Сначала пробуем явно заданный порт, затем все доступные ESP32-S3 CDC.
    candidate_ports = []
    if control_port:
        candidate_ports.append(control_port)

    for p in list_ports.comports():
        # ESP32-S3 USB CDC обычно имеет VID:PID = 303A:1001
        if (getattr(p, "vid", None), getattr(p, "pid", None)) == (0x303A, 0x1001):
            if p.device not in candidate_ports:
                candidate_ports.append(p.device)

    if not candidate_ports:
        print("[OTA-AUTO] Не найдено подходящих COM-портов для отправки 'ota on'")
        return

    last_error = None
    for port in candidate_ports:
        try:
            print(f"[OTA-AUTO] Отправляю 'ota on' в {port} @ {baud}")
            with serial.Serial(port, baudrate=baud, timeout=1, write_timeout=1) as ser:
                # Небольшая пауза на открытие CDC порта
                time.sleep(0.6)
                ser.write(b"\n")
                ser.write(b"ota on\n")
                ser.flush()
                time.sleep(1.2)
            print(f"[OTA-AUTO] Команда отправлена через {port}")
            return
        except Exception as exc:
            last_error = exc
            print(f"[OTA-AUTO] Порт {port} недоступен: {exc}")

    print(f"[OTA-AUTO] Не удалось отправить 'ota on' ни в один порт. Последняя ошибка: {last_error}")
    print("[OTA-AUTO] Проверьте, что Serial Monitor закрыт и COM-порт не занят другой программой")


env.AddPreAction("upload", _send_ota_on)
