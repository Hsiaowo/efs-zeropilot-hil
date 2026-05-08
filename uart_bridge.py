import socket, serial, struct

UDP_PORT = 18002 # TODO CONFIRM: not clashing with :18000/:18001/:50005
SERIAL_PORT = "/dev/ttyUSB0" # TODO CONFIRM: ls /dev/tty* on RPi
BAUD_RATE = 115200 # TODO CONFIRM: match ESP32 firmware

FAKE_VOLTAGE_MV = 12400
FAKE_CURRENT_MA = 1800

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", UDP_PORT))
ser = serial.Serial(SERIAL_PORT, BAUD_RATE)

def crc8(data):
    crc = 0
    for b in data: crc ^= b
    return crc

def build_packet(alt, roll, pitch, yaw, airspeed):
    payload = struct.pack('>B5h2H',
        0x01,
        int(alt * 100),
        int(roll * 100),
        int(pitch * 100),
        int(yaw * 100),
        int(airspeed * 100),
        FAKE_VOLTAGE_MV,
        FAKE_CURRENT_MA,
    )
    return bytes([0xAA]) + payload + bytes([crc8(payload), 0x55])

while True:
    raw, _ = sock.recvfrom(1024)
    alt, roll, pitch, yaw, airspeed = struct.unpack('>5d', raw)
    ser.write(build_packet(alt, roll, pitch, yaw, airspeed))