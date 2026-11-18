"""
DWINETRA combined test: ESP32 band + camera.

How it works
  * The band is the PRIMARY detector. When it reports a fall, an alert is sent
    immediately. The camera never blocks or cancels it.
  * The camera is extra information. The alert says what the camera sees at
    that moment, and a short follow-up says whether the camera confirmed the
    fall within the next few seconds.

Phone alerts go through Telegram (see the setup steps at the top of
telegram_alert.py). Without it the alert is still printed and a snapshot is
saved to ./alerts
"""

import json
import socket
import sys
import time
from pathlib import Path

import telegram_alert

CAMERA_DIR = Path(__file__).resolve().parent.parent / "camera"
sys.path.insert(0, str(CAMERA_DIR))

LISTEN_PORT = 5005  # must match WIFI_STATUS_PORT in the ESP32 secrets.h
MODEL_PATH = CAMERA_DIR / "pose_landmarker.task"
ALERT_DIR = Path(__file__).resolve().parent / "alerts"

# ---------------------------------------------------------------------------
# Settings
# ---------------------------------------------------------------------------

# After an alert, keep watching the camera this long and report whether it
# confirmed the fall.
CAMERA_FOLLOW_UP_S = 10.0

# Ignore further band fall messages for this long after an alert.
ALERT_COOLDOWN_S = 15.0

# Warn if the band sends nothing for this long (battery dead, Wi-Fi lost).
BAND_OFFLINE_S = 15.0

# Show "FALL ALERT SENT" on the video for this long.
ALERT_BANNER_S = 8.0

# Print a line for every raw reading / activity message from the band.
PRINT_READINGS = True


# ---------------------------------------------------------------------------
# Alerts
# ---------------------------------------------------------------------------

def describe_camera(camera_flag, body_angle):
    """Plain-English description of what the camera sees right now."""
    if camera_flag:
        return "camera ALSO sees a fallen posture"
    if body_angle is None:
        return "camera cannot see the person"
    return f"camera sees no fall posture (torso angle {body_angle:.0f} deg)"


def raise_alert(event, frame):
    import cv2

    text = (
        "FALL DETECTED by band | "
        f"sensor peak {event['peak_g']} g | "
        f"{event['camera_at_alert']} | "
        f"{time.strftime('%d %b %Y %H:%M:%S')}"
    )

    print("=" * 70, flush=True)
    print(f"ALERT: {text}", flush=True)
    print("=" * 70, flush=True)

    snapshot = None
    try:
        ALERT_DIR.mkdir(exist_ok=True)
        snapshot = ALERT_DIR / f"fall_{time.strftime('%Y%m%d_%H%M%S')}.jpg"
        if cv2.imwrite(str(snapshot), frame):
            print(f"[alert] Snapshot saved: {snapshot}", flush=True)
        else:
            snapshot = None
    except Exception as error:
        snapshot = None
        print(f"[alert] Could not save snapshot: {error}", flush=True)

    # Sent in the background, in order: photo + text first, follow-up later.
    telegram_alert.send_alert(text, str(snapshot) if snapshot else None)


# ---------------------------------------------------------------------------
# Band fall events + camera follow-up
# ---------------------------------------------------------------------------

class FallTracker:
    """Handles band fall messages and the camera follow-up."""

    def __init__(self):
        self.seen_ids = {}
        self.last_alert_time = None
        self.follow_up = None

    def on_sensor_fall(self, message, now, camera_flag, body_angle):
        """Return an event dict if an alert should be raised, else None.

        The band sends every fall message 3 times, so repeats are ignored.
        """
        self.seen_ids = {
            event_id: seen_at
            for event_id, seen_at in self.seen_ids.items()
            if now - seen_at < 60.0
        }

        event_id = message.get("id")
        if event_id is not None:
            if event_id in self.seen_ids:
                return None
            self.seen_ids[event_id] = now

        if (
            self.last_alert_time is not None
            and now - self.last_alert_time < ALERT_COOLDOWN_S
        ):
            print("Band fall message ignored (cooldown).", flush=True)
            return None

        self.last_alert_time = now

        event = {
            "id": event_id,
            "peak_g": message.get("peak_g"),
            "start": now,
            "person_seen": body_angle is not None,
            "camera_at_alert": describe_camera(camera_flag, body_angle),
        }

        # If the camera already agrees there is nothing left to wait for.
        self.follow_up = None if camera_flag else event
        return event

    def update(self, now, camera_flag, body_angle):
        """Call once per camera frame while a follow-up is pending.

        Returns None, or (result, event) where result is
        'confirmed', 'not_confirmed_seen' or 'not_confirmed_not_visible'.
        """
        event = self.follow_up
        if event is None:
            return None

        if body_angle is not None:
            event["person_seen"] = True

        if camera_flag:
            self.follow_up = None
            return "confirmed", event

        if now - event["start"] >= CAMERA_FOLLOW_UP_S:
            self.follow_up = None
            if event["person_seen"]:
                return "not_confirmed_seen", event
            return "not_confirmed_not_visible", event

        return None

    def follow_up_text(self, now):
        if self.follow_up is None:
            return None
        remaining = max(0.0, CAMERA_FOLLOW_UP_S - (now - self.follow_up["start"]))
        return f"Alert sent - waiting for camera to confirm ({remaining:.0f}s)"

    def banner_active(self, now):
        return (
            self.last_alert_time is not None
            and now - self.last_alert_time < ALERT_BANNER_S
        )


FOLLOW_UP_MESSAGES = {
    "confirmed": "Camera now CONFIRMS the fall.",
    "not_confirmed_seen": (
        "Camera did NOT see a fall posture. The band alert may be a false "
        "alarm - please check."
    ),
    "not_confirmed_not_visible": (
        "Camera never saw the person, so it could not confirm. "
        "The band alert still stands - please check."
    ),
}


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

def main():
    import cv2
    from main import VisionFallDetector  # camera/main.py

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

    tracker = FallTracker()
    last_camera_flag = None
    last_band_message_time = None
    band_offline_reported = False

    try:
        while True:
            success, frame = camera.read()
            if not success:
                print("Could not read a camera frame.")
                break

            now = time.monotonic()
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

                last_band_message_time = now
                if band_offline_reported:
                    print("Band is back online.", flush=True)
                    telegram_alert.send_text("Band is back online.")
                    band_offline_reported = False

                message_type = message.get("type")

                if message_type == "reading" and PRINT_READINGS:
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

                elif message_type == "activity" and PRINT_READINGS:
                    print(
                        f"Activity: {message.get('class')} | "
                        f"Peak acceleration: {message.get('peak_g')} g",
                        flush=True,
                    )

                elif message_type == "fall":
                    event = tracker.on_sensor_fall(
                        message, now, camera_flag, body_angle
                    )
                    if event is not None:
                        raise_alert(event, frame)

            # Camera follow-up after an alert.
            finished = tracker.update(now, camera_flag, body_angle)
            if finished is not None:
                result, event = finished
                follow_up = FOLLOW_UP_MESSAGES[result]
                print(f"FOLLOW-UP | {follow_up}", flush=True)
                telegram_alert.send_text(f"Follow-up: {follow_up}")

            # Band offline warning.
            if (
                last_band_message_time is not None
                and not band_offline_reported
                and now - last_band_message_time > BAND_OFFLINE_S
            ):
                print(
                    f"WARNING: no message from the band for {BAND_OFFLINE_S:.0f} s "
                    "(battery, Wi-Fi or power problem).",
                    flush=True,
                )
                telegram_alert.send_text(
                    f"WARNING: no message from the band for {BAND_OFFLINE_S:.0f} s "
                    "(battery, Wi-Fi or power problem)."
                )
                band_offline_reported = True

            # Status text on the video.
            if last_band_message_time is None:
                band_text = "Band: waiting for first message"
            elif band_offline_reported:
                band_text = "Band: OFFLINE"
            else:
                band_text = "Band: online"

            cv2.putText(frame, band_text, (30, 120),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)

            if tracker.banner_active(now):
                cv2.putText(frame, "FALL ALERT SENT", (30, 160),
                            cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 0, 255), 3)

            follow_up_text = tracker.follow_up_text(now)
            if follow_up_text:
                cv2.putText(frame, follow_up_text, (30, 195),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 165, 255), 2)

            cv2.imshow("DWINETRA Combined Test", frame)

            if cv2.waitKey(1) & 0xFF == ord("q"):
                break

    finally:
        telegram_alert.wait_until_sent()
        receiver.close()
        camera.release()
        detector.close()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()