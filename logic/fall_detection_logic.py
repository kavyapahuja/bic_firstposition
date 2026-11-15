import json
import socket
from pathlib import Path
import sys

import cv2

CAMERA_DIR = Path(__file__).resolve().parent.parent / "camera"
sys.path.insert(0, str(CAMERA_DIR))

from main import VisionFallDetector


LISTEN_PORT = 5005
MODEL_PATH = CAMERA_DIR / "pose_landmarker.task"

def main():
    if not MODEL_PATH.is_file():
        print(f"Pose model not found: {MODEL_PATH}")
        return

    detector = VisionFallDetector(model_path=str(MODEL_PATH))
    camera = cv2.VideoCapture(0)

    if not camera.isOpened():
        print("Could not open the camera.")
        detector.close()
        return

    receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    receiver.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    receiver.bind(("0.0.0.0", LISTEN_PORT))
    receiver.setblocking(False)

    print(f"Listening for ESP32 messages on UDP port {LISTEN_PORT}.")
    print("Camera is running. Press q in the camera window to quit.")

    last_camera_flag = None

    try:
        while True:
            success, frame = camera.read()
            if not success:
                print("Could not read a camera frame.")
                break

            frame, camera_flag, body_angle = detector.process_frame(frame)

            # Print the camera status only when it changes.
            if camera_flag != last_camera_flag:
                print(
                    f"Camera posture flag: {camera_flag}"
                    + (
                        f" | body angle: {body_angle:.1f} degrees"
                        if body_angle is not None
                        else ""
                    ),
                    flush=True,
                )
                last_camera_flag = camera_flag

            # Handle any ESP32 messages waiting on Wi-Fi.
            while True:
                try:
                    data, _sender_address = receiver.recvfrom(2048)
                except BlockingIOError:
                    break

                try:
                    message = json.loads(data.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    print("Received an unreadable ESP32 message.")
                    continue

                message_type = message.get("type")

                if message_type == "reading":
                    print(
                        "READING | "
                        f"ax={message.get('ax_g')} "
                        f"ay={message.get('ay_g')} "
                        f"az={message.get('az_g')} g | "
                        f"gx={message.get('gx_dps')} "
                        f"gy={message.get('gy_dps')} "
                        f"gz={message.get('gz_dps')} degrees/second",
                        flush=True,
                    )

                elif message_type == "activity":
                    print(
                        f"Activity: {message.get('class')} | "
                        f"Peak acceleration: {message.get('peak_g')} g",
                        flush=True,
                    )

                 elif message_type == "fall":
                    print(
                        "SENSOR FALL CANDIDATE | "
                        f"Camera posture flag (info only): {camera_flag} | "
                        "FINAL RESULT: NOT YET CONFIRMED | "
                        f"Peak acceleration: {message.get('peak_g')} g",
                        flush=True,
                    )

            cv2.imshow("DWINETRA Combined Test", frame)

            if cv2.waitKey(1) & 0xFF == ord("q"):
                break

    finally:
        receiver.close()
        camera.release()
        detector.close()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()