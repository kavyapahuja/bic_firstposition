import json
import socket

# Keep this port the same as WIFI_STATUS_PORT in the ESP32 secrets.h.
LISTEN_PORT = 5005

seen_fall_ids = set()

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

        message_type = message.get("type")

        if message_type == "reading":
            print(
                "READING | "
                f"ax={message.get('ax_g')} ay={message.get('ay_g')} "
                f"az={message.get('az_g')} g | "
                f"gx={message.get('gx_dps')} "
                f"gy={message.get('gy_dps')} "
                f"gz={message.get('gz_dps')} degrees/second",
                flush=True,
            )

        elif message_type == "fall":
            # The band sends each fall 3 times; show it once.
            event_id = message.get("id")
            if event_id is not None:
                if event_id in seen_fall_ids:
                    continue
                seen_fall_ids.add(event_id)

            print(
                "SENSOR FALL DETECTED | "
                f"id={event_id} | "
                f"peak acceleration={message.get('peak_g')} g",
                flush=True,
            )

        elif message_type == "activity":
            print(
                f"Activity: {message.get('class')} | "
                f"Peak acceleration: {message.get('peak_g')} g",
                flush=True,
            )