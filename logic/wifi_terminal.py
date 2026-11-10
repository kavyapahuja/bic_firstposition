import json
import socket

# TODO: Keep this port the same in the ESP32 Wi-Fi sender.
LISTEN_PORT = 5005

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
    receiver.bind(("0.0.0.0", LISTEN_PORT))
    print(f"Waiting for DWINETRA messages on UDP port {LISTEN_PORT}...")

    while True:
        data, _sender_address = receiver.recvfrom(2048)

        try:
            message = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            print("Received a message that could not be read.")
            continue

        if message.get("type") == "reading":
            print(
                "READING | "
                f"ax={message.get('ax_g')} ay={message.get('ay_g')} "
                f"az={message.get('az_g')} g | "
                f"gx={message.get('gx_dps')} "
                f"gy={message.get('gy_dps')} "
                f"gz={message.get('gz_dps')} degrees/second",
                flush=True,
            )

        elif message.get("type") == "fall":
            print(
                "FALL DETECTED | "
                f"peak acceleration={message.get('peak_g')} g",
                flush=True,
            )

        elif message.get("type") == "activity":
            print(
                f"Activity: {message.get('class')} | "
                f"Peak acceleration: {message.get('peak_g')} g",
                flush=True,
            )