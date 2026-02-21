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
bool bleEnabled = false;
volatile bool bleConnected = false;
volatile bool bleWelcomePending = false;
volatile bool bleDisconnectPending = false;
unsigned long bleLastHeartbeatMs = 0;
constexpr unsigned long BLE_HEARTBEAT_INTERVAL_MS = 5000;

String rxLineBuffer;

constexpr size_t COMMAND_QUEUE_SIZE = 6;
String commandQueue[COMMAND_QUEUE_SIZE];
volatile uint8_t queueHead = 0;
volatile uint8_t queueTail = 0;
volatile uint8_t queueCount = 0;
portMUX_TYPE bleQueueMux = portMUX_INITIALIZER_UNLOCKED;

void enqueueCommand(const String& cmd) {
    if (cmd.length() == 0) return;

    portENTER_CRITICAL(&bleQueueMux);
    if (queueCount < COMMAND_QUEUE_SIZE) {
        commandQueue[queueTail] = cmd;
        queueTail = (queueTail + 1) % COMMAND_QUEUE_SIZE;
        queueCount++;
    }
    portEXIT_CRITICAL(&bleQueueMux);
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
        if (value.empty()) return;

        bool gotTerminator = false;

        for (char c : value) {
            if (c == '\r' || c == '\n') {
                gotTerminator = true;
                rxLineBuffer.trim();
                if (rxLineBuffer.length() > 0) {
                    enqueueCommand(rxLineBuffer);
                    rxLineBuffer = "";
                }
            } else {
                rxLineBuffer += c;
                if (rxLineBuffer.length() > 128) {
                    rxLineBuffer = "";
                }
            }
        }

        // Позволяем отправлять команду одним write без \n/\r
        if (!gotTerminator) {
            rxLineBuffer.trim();
            if (rxLineBuffer.length() > 0) {
                enqueueCommand(rxLineBuffer);
                rxLineBuffer = "";
            }
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
        Serial.println("[Bluetooth] Уже включен");
        return;
    }

    BLEDevice::init(BLE_DEVICE_NAME);
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new ServerCallbacks());

    BLEService* service = bleServer->createService(NUS_SERVICE_UUID);

    txCharacteristic = service->createCharacteristic(
        NUS_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    txCharacteristic->addDescriptor(new BLE2902());

    BLECharacteristic* rxCharacteristic = service->createCharacteristic(
        NUS_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    rxCharacteristic->setCallbacks(new RxCallbacks());

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
    bleLastHeartbeatMs = millis();

    Serial.println("[Bluetooth] Включен (NUS), ожидаю подключение телефона");
}

void bleTerminalDisable() {
    if (!bleEnabled) {
        Serial.println("[Bluetooth] Уже выключен");
        return;
    }

    BLEDevice::stopAdvertising();
    BLEDevice::deinit(true);

    bleServer = nullptr;
    txCharacteristic = nullptr;
    bleConnected = false;
    bleEnabled = false;
    bleWelcomePending = false;
    bleDisconnectPending = false;
    bleLastHeartbeatMs = 0;

    portENTER_CRITICAL(&bleQueueMux);
    queueHead = queueTail = queueCount = 0;
    portEXIT_CRITICAL(&bleQueueMux);
    rxLineBuffer = "";

    Serial.println("[Bluetooth] Выключен");
}

bool bleTerminalIsEnabled() {
    return bleEnabled;
}

void bleTerminalProcess() {
    if (bleEnabled && bleDisconnectPending) {
        bleDisconnectPending = false;
        Serial.println("\n[Bluetooth] Телефон отключен\n");
    }

    if (bleEnabled && bleConnected && bleWelcomePending) {
        bleWelcomePending = false;
        Serial.println("\n[Bluetooth] Телефон подключен\n");
        bleTerminalLog("\n[Bluetooth] Connected. Commands: bon, boff, reset\n");
        bleLastHeartbeatMs = millis();
    }

    if (bleEnabled && bleConnected && (millis() - bleLastHeartbeatMs >= BLE_HEARTBEAT_INTERVAL_MS)) {
        bleLastHeartbeatMs = millis();
        bleTerminalLog("[Bluetooth] alive\n");
    }
}

bool bleTerminalHasCommand() {
    return queueCount > 0;
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

    return out;
}

void bleTerminalLog(const String &message) {
    if (!bleEnabled || message.length() == 0) return;
    notifyChunk(message.c_str(), message.length());
}
