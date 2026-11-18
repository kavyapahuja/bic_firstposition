"""
Telegram alerts for DWINETRA (no extra packages needed).

SETUP (about 5 minutes)
  1. In Telegram, open @BotFather, send /newbot, follow the steps and copy the
     bot token.
  2. In a terminal, in this folder, run:

         python3 telegram_alert.py --setup

     It asks for the token, tells you which bot to open on your phone, finds
     your chat id by itself, saves telegram_secrets.py, adds it to .gitignore
     and sends a test message.

Other commands
     python3 telegram_alert.py --test         send a test message
     python3 telegram_alert.py --find-chat    list chat ids that messaged the bot

Never upload telegram_secrets.py to GitHub: anyone with the token can use
your bot.
"""

import argparse
import json
import os
import queue
import ssl
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

API_URL = "https://api.telegram.org/bot{token}/{method}"
TIMEOUT_S = 15
RETRIES = 2            # extra attempts after the first one
RETRY_DELAY_S = 2.0

_not_configured_reported = False
_fallback_context = None  # set if Python's own certificates are not enough


# ---------------------------------------------------------------------------
# Secure connection
# ---------------------------------------------------------------------------

def _mac_keychain_context():
    """On a Mac, build an SSL context from the certificates the Mac trusts.

    Some networks (college, office) and some Python installs make Python
    reject Telegram's certificate while the Mac itself accepts it.
    """
    if sys.platform != "darwin":
        return None
    try:
        pem = ""
        for extra in ([], ["/System/Library/Keychains/SystemRootCertificates.keychain"]):
            result = subprocess.run(
                ["security", "find-certificate", "-a", "-p", *extra],
                capture_output=True, text=True, timeout=30,
            )
            pem += result.stdout
        context = ssl.create_default_context()
        context.load_verify_locations(cadata=pem)
        return context
    except Exception:
        return None


def _urlopen(request):
    """urlopen with one automatic fallback for certificate problems."""
    global _fallback_context
    try:
        return urllib.request.urlopen(
            request, timeout=TIMEOUT_S, context=_fallback_context
        )
    except urllib.error.URLError as error:
        if _fallback_context is None and isinstance(
            error.reason, ssl.SSLCertVerificationError
        ):
            context = _mac_keychain_context()
            if context is not None:
                print("[telegram] Using the Mac's own certificates.", flush=True)
                _fallback_context = context
                return urllib.request.urlopen(
                    request, timeout=TIMEOUT_S, context=context
                )
        raise


# ---------------------------------------------------------------------------
# Credentials
# ---------------------------------------------------------------------------

def _load_credentials():
    """Return (token, [chat ids]). Environment variables win over the file."""
    token = os.environ.get("TELEGRAM_BOT_TOKEN", "").strip()
    chat = os.environ.get("TELEGRAM_CHAT_ID", "").strip()

    if not token or not chat:
        try:
            import telegram_secrets  # next to this file

            token = token or str(getattr(telegram_secrets, "BOT_TOKEN", "")).strip()
            chat = chat or str(getattr(telegram_secrets, "CHAT_ID", "")).strip()
        except ImportError:
            pass

    chat_ids = [part.strip() for part in chat.split(",") if part.strip()]
    return token, chat_ids


def is_configured():
    token, chat_ids = _load_credentials()
    return bool(token and chat_ids)


# ---------------------------------------------------------------------------
# Talking to Telegram
# ---------------------------------------------------------------------------

def _call(token, method, body, content_type):
    """Return (ok, detail, retryable)."""
    request = urllib.request.Request(
        API_URL.format(token=token, method=method),
        data=body,
        headers={"Content-Type": content_type},
    )

    try:
        with _urlopen(request) as response:
            payload = json.loads(response.read().decode("utf-8"))
        return bool(payload.get("ok")), payload.get("description", ""), False

    except urllib.error.HTTPError as error:
        try:
            detail = json.loads(error.read().decode("utf-8")).get("description", "")
        except Exception:
            detail = ""
        # 4xx errors (wrong token, chat not found, ...) will not fix themselves.
        retryable = error.code >= 500 or error.code == 429
        return False, f"HTTP {error.code} {detail}".strip(), retryable

    except Exception as error:  # no internet, timeout, ...
        return False, str(error).replace(token, "<token>"), True


def _call_with_retries(token, method, body, content_type):
    ok, detail = False, ""
    for attempt in range(RETRIES + 1):
        ok, detail, retryable = _call(token, method, body, content_type)
        if ok or not retryable:
            break
        if attempt < RETRIES:
            time.sleep(RETRY_DELAY_S)
    return ok, detail


def _multipart(fields, file_field, file_path):
    boundary = uuid.uuid4().hex
    body = b""

    for name, value in fields.items():
        body += (
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="{name}"\r\n\r\n'
            f"{value}\r\n"
        ).encode("utf-8")

    body += (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="{file_field}"; '
        f'filename="{Path(file_path).name}"\r\n'
        "Content-Type: image/jpeg\r\n\r\n"
    ).encode("utf-8")
    body += Path(file_path).read_bytes()
    body += f"\r\n--{boundary}--\r\n".encode("utf-8")

    return body, f"multipart/form-data; boundary={boundary}"


def _check_configured():
    global _not_configured_reported
    token, chat_ids = _load_credentials()

    if token and chat_ids:
        return token, chat_ids

    if not _not_configured_reported:
        print(
            "[telegram] Not set up, so no phone alerts will be sent. "
            "See the top of telegram_alert.py.",
            flush=True,
        )
        _not_configured_reported = True
    return None, []


def send_message_now(text):
    """Send a text message to every chat id. Blocks. Returns True if all sent."""
    token, chat_ids = _check_configured()
    if not token:
        return False

    all_ok = True
    for chat_id in chat_ids:
        body = urllib.parse.urlencode(
            {"chat_id": chat_id, "text": text[:4096]}
        ).encode("utf-8")
        ok, detail = _call_with_retries(
            token, "sendMessage", body, "application/x-www-form-urlencoded"
        )
        print(
            f"[telegram] message to {chat_id}: "
            + ("sent" if ok else f"FAILED ({detail})"),
            flush=True,
        )
        all_ok = all_ok and ok
    return all_ok


def send_photo_now(photo_path, caption):
    """Send a photo with a caption. Falls back to text if the photo fails."""
    token, chat_ids = _check_configured()
    if not token:
        return False

    all_ok = True
    for chat_id in chat_ids:
        try:
            body, content_type = _multipart(
                {"chat_id": chat_id, "caption": caption[:1024]},
                "photo",
                photo_path,
            )
            ok, detail = _call_with_retries(token, "sendPhoto", body, content_type)
        except OSError as error:
            ok, detail = False, f"could not read photo: {error}"

        if ok:
            print(f"[telegram] alert photo to {chat_id}: sent", flush=True)
            continue

        print(
            f"[telegram] photo to {chat_id} FAILED ({detail}); sending text only",
            flush=True,
        )
        text_body = urllib.parse.urlencode(
            {"chat_id": chat_id, "text": caption[:4096]}
        ).encode("utf-8")
        ok, detail = _call_with_retries(
            token, "sendMessage", text_body, "application/x-www-form-urlencoded"
        )
        print(
            f"[telegram] message to {chat_id}: "
            + ("sent" if ok else f"FAILED ({detail})"),
            flush=True,
        )
        all_ok = all_ok and ok
    return all_ok


# ---------------------------------------------------------------------------
# Background sending (keeps the camera video smooth, keeps messages in order)
# ---------------------------------------------------------------------------

_jobs = queue.Queue()
_worker = None
_worker_lock = threading.Lock()


def _worker_loop():
    while True:
        job = _jobs.get()
        try:
            job()
        except Exception as error:
            print(f"[telegram] unexpected error: {error}", flush=True)
        finally:
            _jobs.task_done()


def _enqueue(job):
    global _worker
    with _worker_lock:
        if _worker is None:
            _worker = threading.Thread(target=_worker_loop, daemon=True)
            _worker.start()
    _jobs.put(job)


def send_text(text):
    """Queue a text message. Returns immediately."""
    _enqueue(lambda: send_message_now(text))


def send_alert(text, photo_path=None):
    """Queue an alert. With a photo, the text becomes its caption."""
    if photo_path:
        _enqueue(lambda: send_photo_now(photo_path, text))
    else:
        _enqueue(lambda: send_message_now(text))


def wait_until_sent(timeout_s=20.0):
    """Call before exiting so a message that was just queued is not lost."""
    deadline = time.monotonic() + timeout_s
    while _jobs.unfinished_tasks and time.monotonic() < deadline:
        time.sleep(0.1)


# ---------------------------------------------------------------------------
# Setup helpers
# ---------------------------------------------------------------------------

def _get_json(token, method):
    """Return (payload, error_text)."""
    request = urllib.request.Request(API_URL.format(token=token, method=method))
    try:
        with _urlopen(request) as response:
            return json.loads(response.read().decode("utf-8")), ""
    except urllib.error.HTTPError as error:
        if error.code in (401, 404):
            return None, "Telegram rejected this token. Copy it again from @BotFather."
        return None, f"HTTP {error.code}"
    except Exception as error:
        return None, str(error).replace(token, "<token>")


def _fetch_chats(token):
    """Return ({chat_id: description}, error_text) for everyone who messaged the bot."""
    payload, error = _get_json(token, "getUpdates")
    if payload is None:
        return None, error

    chats = {}
    for update in payload.get("result", []):
        message = update.get("message") or update.get("channel_post") or {}
        chat = message.get("chat")
        if chat:
            name = chat.get("title") or " ".join(
                part for part in (chat.get("first_name"), chat.get("last_name")) if part
            )
            chats[chat["id"]] = f"{chat.get('type')} | {name or chat.get('username', '')}"
    return chats, ""


def _ensure_gitignored(secrets_path):
    """If the project uses git, add the secrets file to .gitignore."""
    for folder in [secrets_path.parent, *secrets_path.parent.parents]:
        if not (folder / ".git").exists():
            continue

        entry = secrets_path.relative_to(folder).as_posix()
        gitignore = folder / ".gitignore"
        text = gitignore.read_text(encoding="utf-8") if gitignore.exists() else ""
        lines = [line.strip() for line in text.splitlines()]

        if entry in lines or "telegram_secrets.py" in lines:
            return f"already listed in {gitignore}"

        with gitignore.open("a", encoding="utf-8") as handle:
            if text and not text.endswith("\n"):
                handle.write("\n")
            handle.write(entry + "\n")
        return f"added to {gitignore}"
    return None


def _find_chat_ids():
    token, _chat_ids = _load_credentials()
    if not token:
        print("No bot token saved yet. Run:  python3 telegram_alert.py --setup")
        return

    chats, error = _fetch_chats(token)
    if chats is None:
        print(f"Could not reach Telegram: {error}")
        return

    if not chats:
        print(
            "No messages found. Open your bot in Telegram, press Start or send "
            '"hi", then run this again.'
        )
        return

    print("Chats that have messaged your bot:")
    for chat_id, description in chats.items():
        print(f"  CHAT_ID = {chat_id}   ({description})")


def _setup():
    print("DWINETRA Telegram setup")
    print("=======================\n")

    token = input(
        "1) Paste your bot token from @BotFather, then press Enter:\n   "
    ).strip().strip('"').strip("'")
    if not token:
        print("No token entered. Setup stopped.")
        return

    payload, error = _get_json(token, "getMe")
    if payload is None or not payload.get("ok"):
        print(f"\nThat token did not work: {error or 'Telegram refused it.'}")
        return

    username = payload.get("result", {}).get("username", "")
    print(f"\nToken OK. Your bot is @{username}")
    print(f"\n2) On your phone, open:  https://t.me/{username}")
    print("   Press Start, then send the message:  hi")
    print("   (Anyone else who should get alerts must do this too.)")

    chats = {}
    for _attempt in range(3):
        input("\n   When you have done that, press Enter here...")
        chats, error = _fetch_chats(token)
        if chats:
            break
        print(
            "   No messages found yet. "
            + (error or "Make sure you messaged YOUR bot, not BotFather.")
        )

    if not chats:
        print("\nSetup stopped. Send hi to your bot, then run --setup again.")
        return

    chat_ids = list(chats)
    print("\n3) People who messaged the bot:")
    for number, chat_id in enumerate(chat_ids, start=1):
        print(f"   {number}. {chats[chat_id]}")

    answer = input(
        "   Send alerts to all of them? Press Enter for yes, or type the "
        "numbers you want (for example 1,3): "
    ).strip()
    if answer:
        picked = []
        for part in answer.replace(" ", "").split(","):
            if part.isdigit() and 1 <= int(part) <= len(chat_ids):
                picked.append(chat_ids[int(part) - 1])
        if not picked:
            print("Nothing valid was selected. Setup stopped.")
            return
        chat_ids = list(dict.fromkeys(picked))

    chat_text = ",".join(str(chat_id) for chat_id in chat_ids)

    secrets_path = Path(__file__).resolve().with_name("telegram_secrets.py")
    secrets_path.write_text(
        "# Created by: python3 telegram_alert.py --setup\n"
        "# Keep this file private. Do not upload it to GitHub.\n\n"
        f"BOT_TOKEN = {token!r}\n"
        f"CHAT_ID = {chat_text!r}\n",
        encoding="utf-8",
    )
    print(f"\nSaved: {secrets_path}")

    git_note = _ensure_gitignored(secrets_path)
    if git_note:
        print(f"Git: telegram_secrets.py {git_note}")

    # Use the new values right away for the test message.
    os.environ["TELEGRAM_BOT_TOKEN"] = token
    os.environ["TELEGRAM_CHAT_ID"] = chat_text

    ok = send_message_now("DWINETRA setup complete: Telegram alerts are working.")
    print(
        "\nDone! Check your phone for the test message."
        if ok
        else "\nSaved, but the test message failed. See the message above."
    )


def _main():
    parser = argparse.ArgumentParser(description="DWINETRA Telegram helper")
    parser.add_argument("--setup", action="store_true",
                        help="guided setup (token, chat id, test message)")
    parser.add_argument("--find-chat", action="store_true",
                        help="list chat ids of people who messaged your bot")
    parser.add_argument("--test", action="store_true",
                        help="send a test message")
    parser.add_argument("--photo", help="with --test: also send this image")
    args = parser.parse_args()

    if args.setup:
        _setup()
    elif args.find_chat:
        _find_chat_ids()
    elif args.test:
        ok = send_message_now("DWINETRA test message: Telegram alerts are working.")
        if args.photo:
            ok = send_photo_now(args.photo, "DWINETRA test photo") and ok
        print("Test finished: " + ("OK" if ok else "something failed, see above"))
    else:
        parser.print_help()


if __name__ == "__main__":
    sys.exit(_main())