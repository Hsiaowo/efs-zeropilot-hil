// don't need to import serial because it's built in
#include <Wire.h> // I2C library

// global shared state (written by UART parser, read by I2C and UART TX)
// volatile because they change on interrupt, don't want caches to mess stuff up
volatile int16_t g_alt, g_roll, g_pitch, g_yaw, g_airspeed;

// fake values for now before first read, in real scenario handle with data valid flag
volatile uint16_t g_voltage_mv = 12400;
volatile uint16_t g_current_ma = 1800;

volatile uint8_t g_reg = 0x00;

// fires automatically whenever ZP sends bytes on the I2C bus to address 0x40
void onReceive(int n) { // interrupt driven
    // n is one byte representing the register number Master (ZP) wants to read
    if (n >= 1) {
        g_reg = Wire.read();
    }
}

// fires automatically when ZP reads from I2C after writing a register address
void onRequest() { // interrupt driven
    uint32_t raw = 0;
    // INA228 registers are 24-bit (3 bytes) so use uint_32_t to avoid overflow
    // UL = unsigned long, prevents overflow before division (smaller dt matches larger dt after multiplication)
    switch (g_reg) {
        case 0x05: // fake VBUS register
            // convert voltage into INA228 raw format
            raw = ((uint32_t)(g_voltage_mv * 1000UL / 195)) << 4; 
            break;
        case 0x07: // fake current register
            raw = (uint32_t)g_current_ma * 10; 
            break;
        case 0x08: // fake power register
            raw = (uint32_t)g_voltage_mv * g_current_ma / 32000; 
            break;
        default: 
            raw = 0;
    }
    Wire.write((raw >> 16) & 0xFF); // most significant byte
    Wire.write((raw >> 8) & 0xFF); // middle byte
    Wire.write( raw & 0xFF); // least significant byte
}

// UART parser
// find and replaces
#define PKT_LEN 18 // matches packet length in uart_bridge.py
#define START 0xAA // matches start byte in uart_bridge.py
#define END 0x55 // matches end byte in uart_bridge.py

void parsePacket(uint8_t* buf) {
    // buf[0] = msg type (0x01), buf[1..10] = 5x int16, buf[11..14] = 2x uint16
    g_alt = (int16_t)((buf[1] << 8) | buf[2]); // 2 bytes signed
    g_roll = (int16_t)((buf[3] << 8) | buf[4]);
    g_pitch = (int16_t)((buf[5] << 8) | buf[6]);
    g_yaw = (int16_t)((buf[7] << 8) | buf[8]);
    g_airspeed = (int16_t)((buf[9] << 8) | buf[10]);
    g_voltage_mv = (uint16_t)((buf[11] << 8) | buf[12]); // unsigned
    g_current_ma = (uint16_t)((buf[13] << 8) | buf[14]);
    
    Serial.print("[ESP32} alt=");
    Serial.print(g_alt);
    Serial.print(" roll=");
    Serial.print(g_roll);
    Serial.print(" pitch=");
    Serial.print(g_pitch);
    Serial.print(" yaw=");
    Serial.print(g_yaw);
    
}

// builds a packet from the global state variables and sends it to ZP over UART2
// TODO CONFIRM: does ZP poll ESP32 for flight state, or does ESP32 push?
// assuming ZP sends a request byte 0xFF, ESP32 replies
void handleZP() {
    // TODO CONFIRM tomorrow: what does ZP actually send/expect?
    // placeholder: push every loop if ZP just listens
    // doesn't have struct.pack() so must do this manually
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
    // UART2 for ZP connection, UART0 for RPi connection
    Serial2.write(pkt, 12); // TODO CONFIRM: Serial1 or Serial2 for ZP?
}

void setup() {
    Serial.begin(115200); // UART0: RPi to ESP32 // TODO CONFIRM port
    Serial2.begin(115200); // UART2: ESP32 to ZP // TODO CONFIRM port and baud

    Wire.begin(0x40); // initialize as I2C slave at INA228 default addr // TODO CONFIRM addr
    Wire.onReceive(onReceive);
    Wire.onRequest(onRequest);
    // TODO CONFIRM: ESP32 I2C pins, default GPIO21(SDA), GPIO22(SCL)
    // make sure these match what ZP's INA228 lines are wired to
}

void loop() {
    // UART RX from RPi
    // static so local vars persist btwn calls
    static uint8_t buf[PKT_LEN];
    static int idx = 0;
    static bool synced = false;

    // packet parser
    // using while Serial.available() to drain entire buffer every time, keeping up regardless of timing delays
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
                    // crc
                    uint8_t crc = 0;
                    for (int i = 1; i < PKT_LEN - 2; i++) {
                        crc ^= buf[i];
                    }
                    if (crc == buf[PKT_LEN - 2]) {
                        parsePacket(buf + 1); // passes pointer to byte index 1
                    }
                }
            }
        }
    }

    // UART TX to ZP
    handleZP();
    delay(10); // TODO CONFIRM: push rate,  100Hz = 10ms, adjust to ZP's expected rate
}
