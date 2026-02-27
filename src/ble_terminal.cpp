#include "ble_terminal.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

namespace {
constexpr const char* BLE_DEVICE_NAME = "Nixie Clock BLE";
constexpr const char* NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* NUS_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; // write
constexpr const char* NUS_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; // notify

BLEServer* bleServer = nullptr;
BLECharacteristic* txCharacteristic = nullptr;
BLECharacteristic* rxCharacteristic = nullptr;
bool bleEnabled = false;
volatile bool bleConnected = false;
volatile bool bleWelcomePending = false;
volatile bool bleDisconnectPending = false;
volatile bool bleEnableAnnouncePending = false;
unsigned long bleLastHeartbeatMs = 0;
constexpr unsigned long BLE_HEARTBEAT_INTERVAL_MS = 5000;
bool bleDebugEnabled = false;

String rxLineBuffer;

constexpr size_t COMMAND_QUEUE_SIZE = 6;
String commandQueue[COMMAND_QUEUE_SIZE];
volatile uint8_t queueHead = 0;
volatile uint8_t queueTail = 0;
volatile uint8_t queueCount = 0;
portMUX_TYPE bleQueueMux = portMUX_INITIALIZER_UNLOCKED;

void enqueueCommand(const String& cmd) {
    if (cmd.length() == 0) return;

    bool queued = false;
    portENTER_CRITICAL(&bleQueueMux);
    if (queueCount < COMMAND_QUEUE_SIZE) {
        commandQueue[queueTail] = cmd;
        queueTail = (queueTail + 1) % COMMAND_QUEUE_SIZE;
        queueCount++;
        queued = true;
    }
    portEXIT_CRITICAL(&bleQueueMux);

    if (queued && bleDebugEnabled) {
        Serial.printf("\n[BLE-DBG] enqueue: '%s'", cmd.c_str());
    }

    if (!queued) {
        Serial.printf("\n[Bluetooth] RX queue full, drop: '%s'", cmd.c_str());
    }
}

void flushRxLineBufferToQueue() {
    rxLineBuffer.trim();
    if (rxLineBuffer.length() > 0) {
        enqueueCommand(rxLineBuffer);
        rxLineBuffer = "";
    }
}

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        (void)pServer;
        bleConnected = true;
        bleWelcomePending = true;
        bleDisconnectPending = false;
    }

    void onDisconnect(BLEServer* pServer) override {
        bleConnected = false;
        bleDisconnectPending = true;
        if (bleEnabled && pServer) {
            pServer->startAdvertising();
        }
    }
};

class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        if (!pCharacteristic) return;

        std::string value = pCharacteristic->getValue();
        if (value.empty()) {
            // Fallback для клиентов/стеков, где payload доступен через getData/getLength
            const size_t len = pCharacteristic->getLength();
            const uint8_t* data = pCharacteristic->getData();
            if (data && len > 0) {
                value.assign(reinterpret_cast<const char*>(data), len);
            }
        }

        const std::string uuid = pCharacteristic->getUUID().toString();
        Serial.printf("\n[Bluetooth] onWrite UUID=%s, len=%u", uuid.c_str(), static_cast<unsigned>(value.size()));

        if (value.empty()) {
            Serial.print("\n[Bluetooth] onWrite payload empty");
            return;
        }

        if (bleDebugEnabled) {
            Serial.printf("\n[BLE-DBG] onWrite len=%u", static_cast<unsigned>(value.size()));
            Serial.print("\n[BLE-DBG] hex:");
            for (uint8_t b : value) {
                Serial.printf(" %02X", b);
            }
        }

        bool gotTerminator = false;

        for (uint8_t b : value) {
            char c = static_cast<char>(b);

            if (c == '\r' || c == '\n') {
                gotTerminator = true;
                flushRxLineBufferToQueue();
            } else {
                // Фильтруем служебные/непечатаемые байты от мобильных приложений
                if (b == 0) {
                    continue;
                }
                if (b >= 32 && b <= 126) {
                    rxLineBuffer += c;
                    if (rxLineBuffer.length() > 128) {
                        if (bleDebugEnabled) {
                            Serial.print("\n[BLE-DBG] rx buffer overflow -> clear");
                        }
                        rxLineBuffer = "";
                    }
                }
            }
        }

        // Позволяем отправлять команду одним write без \n/\r
        if (!gotTerminator) {
            flushRxLineBufferToQueue();
        }

        if (bleDebugEnabled) {
            Serial.printf("\n[BLE-DBG] gotTerminator=%s", gotTerminator ? "true" : "false");
        }
    }
};

void notifyChunk(const char* data, size_t len) {
    if (!bleEnabled || !bleConnected || !txCharacteristic || !data || len == 0) return;

    // 20 байт безопасно для дефолтного MTU
    constexpr size_t CHUNK = 20;
    size_t sent = 0;
    while (sent < len) {
        size_t part = (len - sent > CHUNK) ? CHUNK : (len - sent);
        std::string payload(data + sent, part);
        txCharacteristic->setValue(payload);
        txCharacteristic->notify();
        sent += part;
        delay(2);
    }
}
}

void bleTerminalEnable() {
    if (bleEnabled) {
        Serial.println("\n [Bluetooth] Уже включен");
        return;
    }

    BLEDevice::init(BLE_DEVICE_NAME);
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new ServerCallbacks());

    BLEService* service = bleServer->createService(NUS_SERVICE_UUID);

    txCharacteristic = service->createCharacteristic(
        NUS_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR
    );
    txCharacteristic->addDescriptor(new BLE2902());

    rxCharacteristic = service->createCharacteristic(
        NUS_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    auto* rxCallbacks = new RxCallbacks();
    rxCharacteristic->setCallbacks(rxCallbacks);
    // Совместимость: некоторые мобильные клиенты пишут в TX, а не в RX
    txCharacteristic->setCallbacks(rxCallbacks);

    service->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(NUS_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMinPreferred(0x12);

    BLEDevice::startAdvertising();
    bleEnabled = true;
    bleConnected = false;
    bleWelcomePending = false;
    bleDisconnectPending = false;
    bleEnableAnnouncePending = true;
    bleLastHeartbeatMs = millis();

    Serial.print("\n[Bluetooth] Инициализирован");
}

void bleTerminalDisable() {
    if (!bleEnabled) {
        Serial.println("\n[Bluetooth] Уже выключен");
        return;
    }

    BLEDevice::stopAdvertising();
    BLEDevice::deinit(true);

    bleServer = nullptr;
    txCharacteristic = nullptr;
    rxCharacteristic = nullptr;
    bleConnected = false;
    bleEnabled = false;
    bleWelcomePending = false;
    bleDisconnectPending = false;
    bleEnableAnnouncePending = false;
    bleLastHeartbeatMs = 0;

    portENTER_CRITICAL(&bleQueueMux);
    queueHead = queueTail = queueCount = 0;
    portEXIT_CRITICAL(&bleQueueMux);
    rxLineBuffer = "";

    Serial.println("\n[Bluetooth] Выключен");
}

bool bleTerminalIsEnabled() {
    return bleEnabled;
}

void bleTerminalProcess() {
    if (bleEnabled && bleEnableAnnouncePending) {
        bleEnableAnnouncePending = false;
        Serial.println("\n[Bluetooth] Активирован, ожидаю подключение телефона");
    }

    if (bleEnabled && bleDisconnectPending) {
        bleDisconnectPending = false;
        Serial.println("\n[Bluetooth] Телефон отключен\n");
    }

    if (bleEnabled && bleConnected && bleWelcomePending) {
        bleWelcomePending = false;
        Serial.println("\n[Bluetooth] Телефон подключен");
        bleTerminalLog("\n[Bluetooth] Connected. Write command to RX(0002) or TX(0003)");
        bleLastHeartbeatMs = millis();
    }

}

bool bleTerminalHasCommand() {
    bool has = false;
    portENTER_CRITICAL(&bleQueueMux);
    has = (queueCount > 0);
    portEXIT_CRITICAL(&bleQueueMux);
    return has;
}

String bleTerminalReadCommand() {
    String out;

    portENTER_CRITICAL(&bleQueueMux);
    if (queueCount > 0) {
        out = commandQueue[queueHead];
        queueHead = (queueHead + 1) % COMMAND_QUEUE_SIZE;
        queueCount--;
    }
    portEXIT_CRITICAL(&bleQueueMux);

    if (bleDebugEnabled && out.length() > 0) {
        Serial.printf("\n[BLE-DBG] dequeue: '%s'", out.c_str());
    }

    if (out.length() > 0) {
        Serial.printf("\n[Bluetooth] RX cmd: %s", out.c_str());
    }

    return out;
}

void bleTerminalLog(const String &message) {
    if (!bleEnabled || message.length() == 0) return;
    notifyChunk(message.c_str(), message.length());
}

void bleTerminalSetDebug(bool enabled) {
    bleDebugEnabled = enabled;
    Serial.printf("\n[BLE-DBG] %s", bleDebugEnabled ? "ON" : "OFF");
}

bool bleTerminalIsDebugEnabled() {
    return bleDebugEnabled;
}
