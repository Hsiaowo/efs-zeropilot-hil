import socket
import select
import struct

from hil_config import (
    AIRCRAFT_STATE_PACKET_FORMAT,
    AIRCRAFT_STATE_UDP_FORMAT,
    JSBSIM_STATE_PORT,
    LEGACY_UART_BRIDGE_PORT,
    MSG_AIRCRAFT_STATE,
    PACKET_END,
    PACKET_START,
    PI_ESP32_BAUD,
    SERIAL_PORT,
    UDP_HOST,
    AircraftState,
)


def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
    return crc


def build_aircraft_state_packet(state):
    payload = struct.pack(
        AIRCRAFT_STATE_PACKET_FORMAT,
        MSG_AIRCRAFT_STATE,
        *state.to_fixed_packet_values(),
    )
    return bytes([PACKET_START]) + payload + bytes([crc8(payload), PACKET_END])


def decode_aircraft_state(raw):
    if len(raw) == struct.calcsize(AIRCRAFT_STATE_UDP_FORMAT):
        return AircraftState.from_jsbsim_values(struct.unpack(AIRCRAFT_STATE_UDP_FORMAT, raw))

    # Compatibility path for the old test_sender.py packet:
    # alt, roll, pitch, yaw, airspeed. Missing GPS fields are filled with safe defaults.
    if len(raw) == struct.calcsize(">5d"):
        alt, roll, pitch, yaw, airspeed = struct.unpack(">5d", raw)
        return AircraftState(
            altitude_ft=alt,
            airspeed_kts=airspeed,
            pitch_deg=pitch,
            roll_deg=roll,
            heading_deg=yaw,
            latitude_deg=43.4723,
            longitude_deg=-80.5449,
            g_force=1.0,
            roll_rate_rad_s=0.0,
            pitch_rate_rad_s=0.0,
        )

    raise ValueError(f"unexpected UDP payload size: {len(raw)} bytes")


def main():
    import serial

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_HOST, JSBSIM_STATE_PORT))

    # Optional legacy socket so old test_sender.py can still exercise the UART bridge.
    legacy_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    legacy_sock.bind((UDP_HOST, LEGACY_UART_BRIDGE_PORT))
    ser = serial.Serial(SERIAL_PORT, PI_ESP32_BAUD, timeout=0)

    print("--- UART BRIDGE RUNNING ---")
    print(f"Listening for AircraftState on UDP {JSBSIM_STATE_PORT}")
    print(f"Also accepting legacy 5-field packets on UDP {LEGACY_UART_BRIDGE_PORT}")
    print(f"Writing packets to ESP32 on {SERIAL_PORT} @ {PI_ESP32_BAUD}")

    while True:
        readable, _, _ = select.select([sock, legacy_sock], [], [])
        raw, _ = readable[0].recvfrom(1024)

        try:
            state = decode_aircraft_state(raw)
        except ValueError as exc:
            print(f"Dropped packet: {exc}")
            continue

        pkt = build_aircraft_state_packet(state)
        print(
            "Writing AircraftState: "
            f"alt={state.altitude_ft:.2f}ft "
            f"lat={state.latitude_deg:.7f} "
            f"lon={state.longitude_deg:.7f} "
            f"spd={state.airspeed_kts:.2f}kt"
        )
        ser.write(pkt)

        while ser.in_waiting:
            line = ser.readline()
            if line:
                print(line.decode(errors="replace"), end="")


if __name__ == "__main__":
    main()
