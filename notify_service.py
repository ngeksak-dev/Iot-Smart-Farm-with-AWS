"""
Smart Farm notification service
===============================

Sends Telegram messages and email on behalf of the Node-RED dashboard.

The important design choice: **recipients live here, not in the dashboard.**
A rule in Node-RED says "send this to `farm_team`" and never sees a chat ID
or an email address. That keeps personal contact details out of the database
and out of `flows.json`, which matters because Node-RED strips passwords from
exported flows but does not strip anything else.

Configure recipients with the RECIPIENTS environment variable (JSON), e.g.

    RECIPIENTS='{
      "me":        {"label":"My phone",  "telegram":"123456789"},
      "farm_team": {"label":"Farm team", "telegram":"-4001234567",
                    "email":"team@example.com"},
      "reports":   {"label":"Reports",   "email":"me@example.com"}
    }'

A recipient may have telegram, email, or both — a message goes to every
channel that recipient defines.

Run it:
    uvicorn notify_service:app --host 0.0.0.0 --port 8000
"""

import os
import sys
import json
import asyncio
import threading
from typing import Optional, List, Dict, Literal
from contextlib import asynccontextmanager
from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText
from email.mime.base import MIMEBase
from email import encoders

import httpx
import aiosmtplib
import aiomqtt
from fastapi import (FastAPI, BackgroundTasks, Form, UploadFile, File,
                     HTTPException, status, Depends)
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from pydantic import BaseModel

# ──────────────────────────────────────────────────────────────────────
# Configuration
# ──────────────────────────────────────────────────────────────────────

API_SECURITY_TOKEN = os.getenv("API_SECURITY_TOKEN", "change-me")

TELEGRAM_BOT_TOKEN = os.getenv("TELEGRAM_TOKEN", "")

SMTP_HOST = os.getenv("SMTP_HOST", "smtp.gmail.com")
SMTP_PORT = int(os.getenv("SMTP_PORT", "465"))
EMAIL_FROM = os.getenv("EMAIL_FROM", "")
SMTP_PASS = os.getenv("SMTP_PASS", "")

MQTT_HOST = os.getenv("MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USERNAME = os.getenv("MQTT_USERNAME", "mqtt")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "mqtt123")
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "esp32/#")


def _load_recipients() -> Dict[str, dict]:
    raw = os.getenv("RECIPIENTS", "").strip()
    if not raw:
        print("WARNING: no RECIPIENTS configured - nothing can be notified")
        return {}
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as exc:
        print(f"ERROR: RECIPIENTS is not valid JSON ({exc}) - no recipients loaded")
        return {}

    clean: Dict[str, dict] = {}
    for key, val in data.items():
        if not isinstance(val, dict):
            print(f"WARNING: recipient '{key}' ignored - expected an object")
            continue
        if not val.get("telegram") and not val.get("email"):
            print(f"WARNING: recipient '{key}' has neither telegram nor email - ignored")
            continue
        clean[key] = {
            "label": val.get("label", key),
            "telegram": val.get("telegram"),
            "email": val.get("email"),
        }
    print(f"Loaded {len(clean)} recipient(s): {', '.join(clean) or '(none)'}")
    return clean


RECIPIENTS = _load_recipients()

# ──────────────────────────────────────────────────────────────────────
# Auth
# ──────────────────────────────────────────────────────────────────────

security_scheme = HTTPBearer()


def verify_token(creds: HTTPAuthorizationCredentials = Depends(security_scheme)):
    if creds.credentials != API_SECURITY_TOKEN:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid or missing API token",
            headers={"WWW-Authenticate": "Bearer"},
        )
    return creds.credentials


# ──────────────────────────────────────────────────────────────────────
# MQTT listener (optional; lets the service see live topics)
# ──────────────────────────────────────────────────────────────────────

mqtt_messages: Dict[str, str] = {}
mqtt_thread: Optional[threading.Thread] = None
mqtt_loop: Optional[asyncio.AbstractEventLoop] = None


async def mqtt_listener():
    while True:
        try:
            async with aiomqtt.Client(
                hostname=MQTT_HOST, port=MQTT_PORT,
                username=MQTT_USERNAME, password=MQTT_PASSWORD, timeout=10.0,
            ) as client:
                await client.subscribe(MQTT_TOPIC, qos=0)
                print(f"MQTT subscribed to {MQTT_TOPIC}")
                async for msg in client.messages:
                    mqtt_messages[str(msg.topic)] = msg.payload.decode(errors="replace")
        except asyncio.CancelledError:
            break
        except Exception as exc:
            print(f"MQTT listener error: {exc}. Retrying in 5s...")
            await asyncio.sleep(5)


def start_mqtt_worker():
    global mqtt_loop
    if sys.platform == "win32":
        asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())
    mqtt_loop = asyncio.new_event_loop()
    asyncio.set_event_loop(mqtt_loop)
    try:
        mqtt_loop.run_until_complete(mqtt_listener())
    except asyncio.CancelledError:
        pass
    finally:
        mqtt_loop.close()


@asynccontextmanager
async def lifespan(app: FastAPI):
    global mqtt_thread
    if os.getenv("ENABLE_MQTT", "1") == "1":
        mqtt_thread = threading.Thread(target=start_mqtt_worker, daemon=True)
        mqtt_thread.start()
    yield
    if mqtt_loop and mqtt_loop.is_running():
        mqtt_loop.call_soon_threadsafe(mqtt_loop.stop)
    if mqtt_thread:
        mqtt_thread.join(timeout=5)


app = FastAPI(
    title="Smart Farm Notify",
    lifespan=lifespan,
    dependencies=[Depends(verify_token)],
)

# ──────────────────────────────────────────────────────────────────────
# Delivery
# ──────────────────────────────────────────────────────────────────────


async def send_telegram(chat_id: str, text: str) -> tuple[bool, str]:
    if not TELEGRAM_BOT_TOKEN:
        return False, "TELEGRAM_TOKEN is not set"
    url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
    try:
        async with httpx.AsyncClient() as client:
            r = await client.post(url, data={"chat_id": chat_id, "text": text}, timeout=30.0)
        if r.status_code != 200:
            # Telegram explains refusals in the body, which is far more useful
            # than the status code alone.
            return False, f"Telegram {r.status_code}: {r.text[:300]}"
        return True, "ok"
    except Exception as exc:
        return False, f"Telegram exception: {exc}"


async def send_email(to_addr: str, subject: str, body: str,
                     attachments: Optional[List[dict]] = None) -> tuple[bool, str]:
    if not EMAIL_FROM or not SMTP_PASS:
        return False, "EMAIL_FROM / SMTP_PASS are not set"

    message = MIMEMultipart("mixed")
    message["From"] = EMAIL_FROM
    message["To"] = to_addr
    message["Subject"] = subject
    body_part = MIMEMultipart("alternative")
    body_part.attach(MIMEText(body, "plain"))
    message.attach(body_part)

    for att in attachments or []:
        part = MIMEBase("application", "octet-stream")
        part.set_payload(att["content"])
        encoders.encode_base64(part)
        part.add_header("Content-Disposition",
                        f'attachment; filename="{att["filename"]}"')
        message.attach(part)

    try:
        await aiosmtplib.send(
            message, hostname=SMTP_HOST, port=SMTP_PORT,
            use_tls=(SMTP_PORT == 465), start_tls=(SMTP_PORT == 587),
            username=EMAIL_FROM, password=SMTP_PASS,
        )
        return True, "ok"
    except Exception as exc:
        return False, f"SMTP error: {exc}"


# ──────────────────────────────────────────────────────────────────────
# Endpoints
# ──────────────────────────────────────────────────────────────────────


@app.get("/notify/recipients")
async def list_recipients():
    """
    The dashboard calls this to populate its checkboxes. Only keys, labels and
    which channels exist are returned - never the chat id or address itself.
    """
    return {
        "recipients": [
            {
                "key": k,
                "label": v["label"],
                "channels": [c for c in ("telegram", "email") if v.get(c)],
            }
            for k, v in sorted(RECIPIENTS.items())
        ]
    }


@app.post("/notify/send")
async def notify_send(
    recipient: str = Form(...),
    message: str = Form(...),
    subject: str = Form("Smart Farm alert"),
):
    """
    Main entry point used by Node-RED.

    Unlike a fire-and-forget background task, this waits for delivery and
    reports what actually happened, so the dashboard log can tell "accepted"
    apart from "delivered". A wrong bot token shows up immediately instead of
    silently succeeding.
    """
    who = RECIPIENTS.get(recipient)
    if not who:
        raise HTTPException(
            status_code=404,
            detail=f"Unknown recipient '{recipient}'. Configured: {', '.join(RECIPIENTS) or 'none'}",
        )

    results, ok_any = {}, False

    if who.get("telegram"):
        ok, detail = await send_telegram(who["telegram"], message)
        results["telegram"] = detail
        ok_any = ok_any or ok

    if who.get("email"):
        ok, detail = await send_email(who["email"], subject, message)
        results["email"] = detail
        ok_any = ok_any or ok

    if not ok_any:
        # 502: we understood the request but could not deliver it
        raise HTTPException(status_code=502, detail=results)

    return {"status": "sent", "recipient": recipient, "results": results}


@app.post("/notify/test")
async def notify_test(recipient: str = Form(...)):
    return await notify_send(
        recipient=recipient,
        message="Smart Farm test message. If you can read this, notifications are working.",
        subject="Smart Farm test",
    )


# ---------- direct endpoints, handy for curl and one-off scripts ----------


@app.post("/notify/telegram")
async def notify_telegram_direct(target: str = Form(...), message: str = Form(...)):
    ok, detail = await send_telegram(target, message)
    if not ok:
        raise HTTPException(status_code=502, detail=detail)
    return {"status": "sent"}


@app.post("/notify/email")
async def notify_email_direct(
    bt: BackgroundTasks,
    target: str = Form(...),
    subject: str = Form("Alert"),
    body_text: str = Form(""),
    files: Optional[List[UploadFile]] = File(None),
):
    attachments = []
    for f in files or []:
        if getattr(f, "filename", None):
            attachments.append({"filename": f.filename, "content": await f.read()})
    ok, detail = await send_email(target, subject, body_text, attachments)
    if not ok:
        raise HTTPException(status_code=502, detail=detail)
    return {"status": "sent", "attachments": len(attachments)}


@app.get("/notify/mqtt/messages")
async def get_mqtt_messages():
    return {"messages": mqtt_messages}


@app.get("/notify/health")
async def health():
    return {
        "status": "ok",
        "recipients": len(RECIPIENTS),
        "telegram_configured": bool(TELEGRAM_BOT_TOKEN),
        "email_configured": bool(EMAIL_FROM and SMTP_PASS),
    }
