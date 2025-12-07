#!/usr/bin/env python3
import datetime
import re
import os
from pathlib import Path

def update_version(project_dir=None):
    """
    Обновляет версию прошивки в файле config.h.
    Формат: MCU.HW_VARIANT.RELEASE_TYPE.BUILD_DATE
    Пример: 1.A0.3.251206
    """
    # Текущая дата
    build_date = datetime.datetime.now().strftime("%y%m%d")
    
    # Определяем путь к config.h
    if project_dir is None:
        project_dir = os.getcwd()
    
    config_path = os.path.join(project_dir, 'include', 'config.h')
    
    # Проверяем что файл существует
    if not os.path.exists(config_path):
        print(f"❌ Файл {config_path} не найден!")
        print(f"   Текущая директория: {os.getcwd()}")
        print(f"   Проект директория: {project_dir}")
        return False
    
    try:
        # Читаем текущую версию
        with open(config_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Ищем текущую версию (MCU.HW_VARIANT.RELEASE_TYPE.BUILD_DATE)
        # Формат: 1.A0.3.251201 (цифра.буква+цифра.цифра.дата)
        match = re.search(r'#define FIRMWARE_VERSION "(\d)\.([A-Z]\d)\.(\d)\.\d+"', content)
        if not match:
            print("❌ Не удалось найти FIRMWARE_VERSION в config.h")
            print(f"   Текущий формат версии должен быть: MCU.HW_VARIANT.RELEASE_TYPE.BUILD_DATE")
            print(f"   Пример: 1.A0.3.251206")
            return False
        
        mcu, hw_variant, release_type = match.groups()
        new_version = f'{mcu}.{hw_variant}.{release_type}.{build_date}'
        
        # Заменяем версию (используем более точный паттерн)
        new_content = re.sub(
            r'#define FIRMWARE_VERSION "[^"]*"',
            f'#define FIRMWARE_VERSION "{new_version}"',
            content
        )
        
        # Записываем обратно
        with open(config_path, 'w', encoding='utf-8') as f:
            f.write(new_content)
        
        print(f"✅ Версия обновлена: {new_version}")
        return True
        
    except Exception as e:
        print(f"❌ Ошибка при обновлении версии: {e}")
        return False

# Точка входа для PlatformIO
def main(env=None):
    """Главная функция, вызываемая PlatformIO"""
    if env is not None:
        # Используем project_dir от PlatformIO env
        project_dir = env.get('PROJECT_DIR', os.getcwd())
    else:
        project_dir = os.getcwd()
    
    print("[PRE-BUILD] Обновление версии прошивки...", flush=True)
    update_version(project_dir)
    print("[PRE-BUILD] Версия готова к компиляции", flush=True)

# Для работы с PlatformIO
if __name__ == "__main__":
    # Если скрипт запущен напрямую (не через PlatformIO)
    update_version()
else:
    # Если скрипт импортирован/запущен через PlatformIO
    # PlatformIO вызовет main(env) автоматически
    pass