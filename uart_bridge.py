import socket, serial, struct

# listens for data on Simulink with this port
UDP_PORT = 18002 # TODO CONFIRM: not clashing with :18000/:18001/:50005
SERIAL_PORT = "/dev/ttyUSB0" # TODO CONFIRM: ls /dev/tty* on RPi
BAUD_RATE = 115200

# hardcoded fake ina readings...
FAKE_VOLTAGE_MV = 12400 # 12400mV = 12.4V
FAKE_CURRENT_MA = 1800 # 1800mA = 1.8A

# socket.AF_INET = IPv4 addressing, socket.SOCK_DGRAM = use UDP (DGRAM = datagram, fire and forget))
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", UDP_PORT)) # binds to 18002 to recieve UDP packets
ser = serial.Serial(SERIAL_PORT, BAUD_RATE) # opens physical UART port

# corruption check (CRC changes = packet is invalid)
def crc8(data): # data is in bytes
    crc = 0
    for b in data: 
        crc ^= b # XOR is fine over a short wire
    # checked against CRC in ESP32
    return crc

# convert floats (8 bytes) into binary packet of ints (2 bytes)
def build_packet(alt, roll, pitch, yaw, airspeed):
    payload = struct.pack('>B5h2H', # big endian, 1 unsigned byte, 5 signed int16, 2 unsigned int16
        0x01,
        int(alt * 100), # fixed point encoding
        int(roll * 100),
        int(pitch * 100),
        int(yaw * 100),
        int(airspeed * 100),
        FAKE_VOLTAGE_MV,
        FAKE_CURRENT_MA,
    )
    # start byte (1), data (15), CRC byte (1), end byte (1) = 18 bytes packet length
    return bytes([0xAA]) + payload + bytes([crc8(payload), 0x55])

# main loop
while True:
    # blocks until UDP packet arrives, returns (data, address)
    raw, _ = sock.recvfrom(1024) # max bytes to recieve (Simulink gives 5 doubles x 8bytes = 40 bytes)
    
    # unpacks raw bytes
    alt, roll, pitch, yaw, airspeed = struct.unpack('>5d', raw)

    # sends bytes using UART to TX pin on RPi which is connected to RX pin on ESP32
    ser.write(build_packet(alt, roll, pitch, yaw, airspeed))