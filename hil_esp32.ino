#include <Wire.h>

// from power_module.hpp: INA228_ADDR = 0b1000101 = 0x45, not 0x40
#define I2C_ADDR 0x45
#define FAKE_VOLTAGE 12.4f
#define FAKE_CURRENT 1.8f
#define FAKE_POWER (FAKE_VOLTAGE * FAKE_CURRENT)

// from power_module.hpp: exact LSB constants, copied directly so encoding matches ZP's decoding
#define CURRENT_LSB (32.0f / (1 << 19))
#define VBUS_LSB 195.3125e-6f
#define POWER_LSB (3.2f * CURRENT_LSB)
#define ENERGY_LSB (16 * 3.2f * CURRENT_LSB)
#define CHARGE_LSB CURRENT_LSB

// global shared state (written by UART parser, read by I2C)
// volatile because they change in the UART loop, don't want caches to mess stuff up
volatile int16_t g_alt, g_roll, g_pitch, g_yaw, g_airspeed;

// from power_module.cpp: ZP reads charge and energy as accumulated values, so we accumulate over time
static float fakeCharge = 0.0f;
static float fakeEnergy = 0.0f;

// register ZP last requested
static uint8_t g_reg = 0xFF;

// UART packet constants, must match uart_bridge.py
#define PKT_LEN 18
#define START 0xAA
#define END 0x55

// from power_module.hpp: VBUS, CURRENT, POWER are 24 bit (3 bytes), ENERGY and CHARGE are 40 bit (5 bytes)
void packU24(uint8_t* buf, uint32_t val) {
    buf[0] = (val >> 16) & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = val & 0xFF;
}

void packU40(uint8_t* buf, uint64_t val) {
    buf[0] = (val >> 32) & 0xFF;
    buf[1] = (val >> 24) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 8) & 0xFF;
    buf[4] = val & 0xFF;
}

// fires when ZP writes a register address on I2C (telling us what it wants to read next)
void onReceive(int n) {
    if (n >= 1) g_reg = Wire.read();
}

void onRequest() {
    uint8_t buf[5] = {0};
    // from power_module.cpp: ZP reads in this order via DMA callback chain: VBUS -> CURRENT -> POWER -> ENERGY -> CHARGE
    // from power_module.cpp: readData() reverses this encoding, so we must encode with same LSBs and bit shifts
    switch (g_reg) {
        case 0x05: { // from power_module.hpp: REG_VBUS = {0x05, 3 bytes}, 24 bit unsigned, left shifted 4
            uint32_t raw = ((uint32_t)(FAKE_VOLTAGE / VBUS_LSB)) << 4;
            packU24(buf, raw);
            Wire.write(buf, 3);
            break;
        }
        case 0x07: { // from power_module.hpp: REG_CURRENT = {0x07, 3 bytes}, 24 bit signed, left shifted 4
            int32_t raw = ((int32_t)(FAKE_CURRENT / CURRENT_LSB)) << 4;
            packU24(buf, (uint32_t)raw);
            Wire.write(buf, 3);
            break;
        }
        case 0x08: { // from power_module.hpp: REG_POWER = {0x08, 3 bytes}, 24 bit unsigned, no shift
            uint32_t raw = (uint32_t)(FAKE_POWER / POWER_LSB);
            packU24(buf, raw);
            Wire.write(buf, 3);
            break;
        }
        case 0x09: { // from power_module.hpp: REG_ENERGY = {0x09, 5 bytes}, 40 bit unsigned
            uint64_t raw = (uint64_t)(fakeEnergy / ENERGY_LSB);
            packU40(buf, raw);
            Wire.write(buf, 5);
            break;
        }
        case 0x0A: { // from power_module.hpp: REG_CHARGE = {0x0A, 5 bytes}, 40 bit signed
            int64_t raw = (int64_t)(fakeCharge / CHARGE_LSB);
            packU40(buf, (uint64_t)raw);
            Wire.write(buf, 5);
            break;
        }
        default:
            Wire.write(buf, 1);
            break;
    }
}

void parsePacket(uint8_t* buf) {
    // buf[0] = msg type (0x01), buf[1..10] = 5x int16, buf[11..14] = 2x uint16
    g_alt      = (int16_t)((buf[1] << 8) | buf[2]);
    g_roll     = (int16_t)((buf[3] << 8) | buf[4]);
    g_pitch    = (int16_t)((buf[5] << 8) | buf[6]);
    g_yaw      = (int16_t)((buf[7] << 8) | buf[8]);
    g_airspeed = (int16_t)((buf[9] << 8) | buf[10]);
    // dont need buf[11..14] voltage/current anymore bc ZP reads power data over I2C not UART

    Serial.print("[ESP32] alt=");
    Serial.print(g_alt);
    Serial.print(" roll=");
    Serial.print(g_roll);
    Serial.print(" pitch=");
    Serial.print(g_pitch);
    Serial.print(" yaw=");
    Serial.println(g_yaw);
}

void setup() {
    Serial.begin(115200); // UART0: RPi to ESP32 via USB

    Wire.begin(I2C_ADDR); // I2C slave, default pins GPIO21(SDA) GPIO22(SCL)
    Wire.onReceive(onReceive);
    Wire.onRequest(onRequest);
    // dont need Serial2 bc from drivers.cpp confirmed no UART assigned to HIL input on ZP,
    // ZP gets flight state from its own GPS/IMU not from ESP32
}

void loop() {
    // UART RX from RPi
    // static so local vars persist between calls
    static uint8_t buf[PKT_LEN];
    static int idx = 0;
    static bool synced = false;

    // drain entire buffer every loop to keep up regardless of timing delays
    while (Serial.available()) {
        uint8_t b = Serial.read();
        if (!synced) {
            if (b == START) {
                idx = 0;
                buf[idx++] = b;
                synced = true;
            }
        } else {
            buf[idx++] = b;
            if (idx == PKT_LEN) {
                synced = false;
                if (buf[PKT_LEN - 1] == END) {
                    uint8_t crc = 0;
                    for (int i = 1; i < PKT_LEN - 2; i++) crc ^= buf[i];
                    if (crc == buf[PKT_LEN - 2]) parsePacket(buf + 1);
                }
            }
        }
    }

    // from power_module_iface.hpp: charge and energy are accumulated fields, accumulate here so ZP sees realistic values
    fakeCharge += FAKE_CURRENT * (10.0f / 1000.0f); // amps * dt_sec
    fakeEnergy += FAKE_POWER   * (10.0f / 1000.0f); // watts * dt_sec

    // dont need handleZP() bc from drivers.cpp confirmed ZP has no UART input for flight state
    delay(10); // 100Hz loop
}