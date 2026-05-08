#include <Wire.h>

// shared state (written by UART parser, read by I2C and UART TX)
volatile int16_t g_alt, g_roll, g_pitch, g_yaw, g_airspeed;
volatile uint16_t g_voltage_mv = 12400;
volatile uint16_t g_current_ma = 1800;
volatile uint8_t g_reg = 0x00;

// I2C slave (fake INA228)
void onReceive(int n) {
    if (n >= 1) g_reg = Wire.read();
}

void onRequest() {
    uint32_t raw = 0;
    switch (g_reg) {
        case 0x05: 
            raw = ((uint32_t)(g_voltage_mv * 1000UL / 195)) << 4; 
            break;
        case 0x07: 
            raw = (uint32_t)g_current_ma * 10; 
            break;
        case 0x08: 
            raw = (uint32_t)g_voltage_mv * g_current_ma / 32000; 
            break;
        default: 
            raw = 0;
    }
    Wire.write((raw >> 16) & 0xFF);
    Wire.write((raw >> 8) & 0xFF);
    Wire.write( raw & 0xFF);
}

// UART parser
#define PKT_LEN 18
#define START 0xAA
#define END 0x55

void parsePacket(uint8_t* buf) {
    // buf[0] = msg type (0x01), buf[1..10] = 5x int16, buf[11..14] = 2x uint16
    g_alt = (int16_t)((buf[1] << 8) | buf[2]);
    g_roll = (int16_t)((buf[3] << 8) | buf[4]);
    g_pitch = (int16_t)((buf[5] << 8) | buf[6]);
    g_yaw = (int16_t)((buf[7] << 8) | buf[8]);
    g_airspeed = (int16_t)((buf[9] << 8) | buf[10]);
    g_voltage_mv = (uint16_t)((buf[11] << 8) | buf[12]);
    g_current_ma = (uint16_t)((buf[13] << 8) | buf[14]);
}

// ZP flight state sender
// TODO CONFIRM: does ZP poll ESP32 for flight state, or does ESP32 push?
// Assuming ZP sends a request byte 0xFF, ESP32 replies
void handleZP() {
    // TODO CONFIRM tomorrow: what does ZP actually send/expect?
    // Placeholder: push every loop if ZP just listens
    uint8_t pkt[12];
    pkt[0] = 0xBB; // start
    pkt[1] = (g_alt >> 8) & 0xFF;
    pkt[2] =  g_alt & 0xFF;
    pkt[3] = (g_roll >> 8) & 0xFF;
    pkt[4] =  g_roll & 0xFF;
    pkt[5] = (g_pitch >> 8) & 0xFF;
    pkt[6] =  g_pitch & 0xFF;
    pkt[7] = (g_yaw >> 8) & 0xFF;
    pkt[8] =  g_yaw & 0xFF;
    pkt[9] = (g_airspeed >> 8) & 0xFF;
    pkt[10] =  g_airspeed & 0xFF;
    pkt[11] = 0x55;
    Serial2.write(pkt, 12); // TODO CONFIRM: Serial1 or Serial2 for ZP?
}

void setup() {
    Serial.begin(115200);   // UART0: RPi to ESP32 // TODO CONFIRM port and baud
    Serial2.begin(115200);  // UART2: ESP32 to ZP // TODO CONFIRM port and baud

    Wire.begin(0x40); // I2C slave at INA228 default addr // TODO CONFIRM addr
    Wire.onReceive(onReceive);
    Wire.onRequest(onRequest);
    // TODO CONFIRM: ESP32 I2C pins, default GPIO21(SDA), GPIO22(SCL)
    // make sure these match what ZP's INA228 lines are wired to
}

void loop() {
    // UART RX from RPi
    static uint8_t buf[PKT_LEN];
    static int idx = 0;
    static bool synced = false;

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
                    for (int i = 1; i < PKT_LEN - 2; i++) {
                        crc ^= buf[i];
                    }
                    if (crc == buf[PKT_LEN - 2]) {
                        parsePacket(buf + 1);
                    }
                }
            }
        }
    }

    // UART TX to ZP
    handleZP();
    delay(10); // TODO CONFIRM: push rate,  100Hz = 10ms, adjust to ZP's expected rate
}