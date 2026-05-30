import serial

s = serial.Serial('/dev/ttyACM0', 115200)

while True:
	d = s.read_until(bytes([0x55]))
	print([hex(b) for b in d])
	print("---")
