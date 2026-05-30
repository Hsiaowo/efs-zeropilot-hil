import socket, struct, time

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
data = struct.pack('>5d', 100.0, 5.0, 2.0, 45.0, 20.0)

for i in range(10):
	sock.sendto(data, ('127.0.0.1', 18002))
	print(f"Send packet {i+1}")
	time.sleep(0.1)

print("Done")
