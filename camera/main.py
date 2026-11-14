import cv2
import mediapipe as mp
import math
import time

from mediapipe.tasks import python
from mediapipe.tasks.python import vision


def calculate_body_angle(shoulder_mid, hip_mid, frame_width, frame_height):
    """Return torso angle relative to horizontal, in degrees.

    0 degrees is roughly horizontal; 90 degrees is roughly upright.
    The coordinates are normalized, so scale them to pixels before measuring.
    """
    dx = (hip_mid[0] - shoulder_mid[0]) * frame_width
    dy = (hip_mid[1] - shoulder_mid[1]) * frame_height

    return math.degrees(math.atan2(abs(dy), abs(dx)))


def setup_pose_landmarker(model_path="pose_landmarker.task"):
    options = vision.PoseLandmarkerOptions(
        base_options=python.BaseOptions(model_asset_path=model_path),
        running_mode=vision.RunningMode.VIDEO,
    )

    return vision.PoseLandmarker.create_from_options(options)


class VisionFallDetector:
    def __init__(
        self,
        model_path="pose_landmarker.task",
        angle_threshold=50,
        sustain_ms=500,
        max_no_detection_ms=1000,
        min_visibility=0.5,
    ):
        # TODO: Calibrate these posture settings with the actual camera placement.
        self.ANGLE_THRESHOLD = angle_threshold
        self.SUSTAIN_MS = sustain_ms
        self.MAX_NO_DETECTION_MS = max_no_detection_ms
        self.MIN_VISIBILITY = min_visibility

        self.landmarker = setup_pose_landmarker(model_path)

        # MediaPipe VIDEO mode needs increasing timestamps in milliseconds.
        self.frame_timestamp = -1

        self.low_angle_since_ms = None
        self.no_detection_since_ms = None
        self.fall_like = False
        self.body_angle = None

        self.NOSE = 0
        self.LEFT_SHOULDER, self.RIGHT_SHOULDER = 11, 12
        self.LEFT_HIP, self.RIGHT_HIP = 23, 24
        self.LEFT_KNEE, self.RIGHT_KNEE = 25, 26
        self.LEFT_ANKLE, self.RIGHT_ANKLE = 27, 28

    def _next_timestamp_ms(self):
        now_ms = time.monotonic_ns() // 1_000_000
        self.frame_timestamp = max(now_ms, self.frame_timestamp + 1)
        return self.frame_timestamp

    def _is_visible(self, landmark):
        visibility = getattr(landmark, "visibility", None)

        # Some model versions may omit visibility. In that case, retain the point.
        return visibility is None or visibility >= self.MIN_VISIBILITY

    def _mark_pose_missing(self, now_ms):
        if self.no_detection_since_ms is None:
            self.no_detection_since_ms = now_ms

        # Don't count time without a reliable pose toward fall confirmation.
        self.low_angle_since_ms = None
        self.body_angle = None

        if now_ms - self.no_detection_since_ms >= self.MAX_NO_DETECTION_MS:
            self.fall_like = False

    def process_frame(self, frame):
        """Return (annotated_frame, fall_like, body_angle)."""
        now_ms = self._next_timestamp_ms()

        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(
            image_format=mp.ImageFormat.SRGB,
            data=rgb_frame,
        )

        result = self.landmarker.detect_for_video(mp_image, now_ms)

        if not result.pose_landmarks:
            self._mark_pose_missing(now_ms)
            self._draw_status(frame)
            return frame, self.fall_like, self.body_angle

        height, width, _ = frame.shape
        landmarks = result.pose_landmarks[0]

        required = [
            landmarks[self.LEFT_SHOULDER],
            landmarks[self.RIGHT_SHOULDER],
            landmarks[self.LEFT_HIP],
            landmarks[self.RIGHT_HIP],
        ]

        if not all(self._is_visible(point) for point in required):
            self._mark_pose_missing(now_ms)
            self._draw_status(frame)
            return frame, self.fall_like, self.body_angle

        self.no_detection_since_ms = None

        key_points = {
            "nose": landmarks[self.NOSE],
            "left_shoulder": landmarks[self.LEFT_SHOULDER],
            "right_shoulder": landmarks[self.RIGHT_SHOULDER],
            "left_hip": landmarks[self.LEFT_HIP],
            "right_hip": landmarks[self.RIGHT_HIP],
            "left_knee": landmarks[self.LEFT_KNEE],
            "right_knee": landmarks[self.RIGHT_KNEE],
            "left_ankle": landmarks[self.LEFT_ANKLE],
            "right_ankle": landmarks[self.RIGHT_ANKLE],
        }

        for name, landmark in key_points.items():
            if not self._is_visible(landmark):
                continue

            x = int(landmark.x * width)
            y = int(landmark.y * height)

            cv2.circle(frame, (x, y), 7, (0, 255, 0), -1)
            cv2.putText(
                frame,
                name,
                (x + 8, y - 8),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.5,
                (255, 255, 255),
                1,
            )

        shoulder_mid = (
            (key_points["left_shoulder"].x + key_points["right_shoulder"].x) / 2,
            (key_points["left_shoulder"].y + key_points["right_shoulder"].y) / 2,
        )
        hip_mid = (
            (key_points["left_hip"].x + key_points["right_hip"].x) / 2,
            (key_points["left_hip"].y + key_points["right_hip"].y) / 2,
        )

        self.body_angle = calculate_body_angle(
            shoulder_mid,
            hip_mid,
            width,
            height,
        )

        if self.body_angle < self.ANGLE_THRESHOLD:
            if self.low_angle_since_ms is None:
                self.low_angle_since_ms = now_ms

            self.fall_like = (
                now_ms - self.low_angle_since_ms >= self.SUSTAIN_MS
            )
        else:
            self.low_angle_since_ms = None
            self.fall_like = False

        cv2.putText(
            frame,
            f"Body angle: {self.body_angle:.1f}",
            (30, 40),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.8,
            (0, 0, 255),
            2,
        )

        self._draw_status(frame)
        return frame, self.fall_like, self.body_angle

    def _draw_status(self, frame):
        cv2.putText(
            frame,
            f"Camera posture flag: {self.fall_like}",
            (30, 80),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.8,
            (0, 0, 255),
            2,
        )

    def close(self):
        self.landmarker.close()


if __name__ == "__main__":
    camera = cv2.VideoCapture(0)
    detector = VisionFallDetector(model_path="pose_landmarker.task")

    if not camera.isOpened():
        print("Could not open the camera.")
    else:
        print("Camera running. Press q in the camera window to quit.")

        try:
            while True:
                success, frame = camera.read()
                if not success:
                    print("Could not read a camera frame.")
                    break

                annotated_frame, fall_like, body_angle = detector.process_frame(frame)
                cv2.imshow("DWINETRA Camera Test", annotated_frame)

                if cv2.waitKey(1) & 0xFF == ord("q"):
                    break
        finally:
            camera.release()
            detector.close()
            cv2.destroyAllWindows()