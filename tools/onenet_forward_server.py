#!/usr/bin/env python3
"""
Local HTTP forwarder for OneNET rule-engine pushes.

Flow:
  device_A -> OneNET rule engine -> this server -> OneNET property-set API -> device_B

Start locally:
  python tools/onenet_forward_server.py

Expose with cpolar/ngrok:
  cpolar http 3000

Set the public URL in OneNET HTTP push instance:
  https://<your-tunnel-domain>/onenet/forward

Useful environment variables:
  FORWARD_HOST                 default: 127.0.0.1
  FORWARD_PORT                 default: 3000
  FORWARD_SHARED_TOKEN         optional token checked against query/header/body
  FORWARD_REQUIRE_POST_TOKEN   default: 0; set to 1 to require token on POST
  FORWARD_ACTIVE_MIN_MS        default: 100; minimum interval for non-zero forwards
  FORWARD_CLEAR_MIN_MS         default: 1000; minimum interval for clear forwards
  FORWARD_ACCEPT_PAST_MS       default: 5000; accept payloads this old at startup
  FORWARD_LOG_SKIPS            default: 0; set to 1 to log skipped duplicate/idle pushes
  FORWARD_LOG_REQUESTS         default: 0; set to 1 to log every HTTP request
  FORWARD_LOG_DISCONNECTS      default: 0; set to 1 to log client disconnects
  TARGET_DEVICE_NAME           default: device_B
  EXPECTED_SOURCE_ID           default: device_A
  ONENET_PRODUCT_ID            default: 8x5w9DD3Av

  ONENET_SET_PROPERTY_URL      required to really call OneNET; otherwise dry-run
  ONENET_AUTH_HEADER_NAME      default: authorization
  ONENET_AUTH_HEADER_VALUE     value for the auth header, for example your API token
  ONENET_SET_PROPERTY_BODY     optional JSON template for OneNET request body

Default OneNET request body template:
  {
    "product_id": "{product_id}",
    "device_name": "{target_device}",
    "params": {
      "source_id": "{source_id}",
      "max_tx_idx": {max_tx_idx},
      "max_tx_value": {max_tx_value}
    }
  }
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import socket
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any


HOST = os.getenv("FORWARD_HOST", "127.0.0.1")
PORT = int(os.getenv("FORWARD_PORT", "3000"))
PATH = "/onenet/forward"

TARGET_DEVICE_NAME = os.getenv("TARGET_DEVICE_NAME", "device_B")
EXPECTED_SOURCE_ID = os.getenv("EXPECTED_SOURCE_ID", "device_A")
ONENET_PRODUCT_ID = os.getenv("ONENET_PRODUCT_ID", "8x5w9DD3Av")
SHARED_TOKEN = os.getenv("FORWARD_SHARED_TOKEN", "")
REQUIRE_POST_TOKEN = os.getenv("FORWARD_REQUIRE_POST_TOKEN", "0") == "1"
ACTIVE_FORWARD_MIN_MS = int(os.getenv("FORWARD_ACTIVE_MIN_MS", "100"))
CLEAR_FORWARD_MIN_MS = int(os.getenv("FORWARD_CLEAR_MIN_MS", "1000"))
ACCEPT_PAST_MS = int(os.getenv("FORWARD_ACCEPT_PAST_MS", "5000"))
LOG_SKIPS = os.getenv("FORWARD_LOG_SKIPS", "0") == "1"
LOG_REQUESTS = os.getenv("FORWARD_LOG_REQUESTS", "0") == "1"
LOG_DISCONNECTS = os.getenv("FORWARD_LOG_DISCONNECTS", "0") == "1"
START_TIME_MS = int(time.time() * 1000)

last_forward_key: tuple[str, int, int] | None = None
last_forward_ms = 0
last_payload_time = 0
forward_signal_active = False
forward_lock = threading.Lock()

ONENET_SET_PROPERTY_URL = os.getenv("ONENET_SET_PROPERTY_URL", "")
ONENET_AUTH_HEADER_NAME = os.getenv("ONENET_AUTH_HEADER_NAME", "authorization")
ONENET_AUTH_HEADER_VALUE = os.getenv("ONENET_AUTH_HEADER_VALUE", "")
ONENET_SET_PROPERTY_BODY = os.getenv(
    "ONENET_SET_PROPERTY_BODY",
    json.dumps(
        {
            "product_id": "{product_id}",
            "device_name": "{target_device}",
            "params": {
                "source_id": "{source_id}",
                "max_tx_idx": "{max_tx_idx}",
                "max_tx_value": "{max_tx_value}",
            },
        },
        separators=(",", ":"),
    ),
)


def log(message: str) -> None:
    now = time.strftime("%Y-%m-%d %H:%M:%S")
    print(f"[{now}] {message}", flush=True)


def get_nested(data: Any, *path: str) -> Any:
    cur = data
    for key in path:
        if not isinstance(cur, dict) or key not in cur:
            return None
        cur = cur[key]
    return cur


def unwrap_value(value: Any) -> Any:
    if isinstance(value, dict) and "value" in value:
        return value["value"]
    return value


def find_params(payload: dict[str, Any]) -> dict[str, Any]:
    payload = normalize_push_payload(payload)
    candidates = [
        get_nested(payload, "data", "params"),
        get_nested(payload, "params"),
        get_nested(payload, "msg", "params"),
        get_nested(payload, "body", "params"),
        payload,
    ]
    for item in candidates:
        if isinstance(item, dict) and (
            "source_id" in item or "max_tx_idx" in item or "max_tx_value" in item
        ):
            return item
    return {}


def normalize_push_payload(payload: dict[str, Any]) -> dict[str, Any]:
    msg = payload.get("msg")
    if isinstance(msg, str):
        try:
            parsed = json.loads(msg)
            if isinstance(parsed, dict):
                return parsed
        except json.JSONDecodeError:
            return payload
    return payload


def extract_forward_data(payload: dict[str, Any]) -> tuple[str, int, int]:
    payload = normalize_push_payload(payload)
    params = find_params(payload)

    source_id = unwrap_value(params.get("source_id"))
    max_tx_idx = unwrap_value(params.get("max_tx_idx"))
    max_tx_value = unwrap_value(params.get("max_tx_value"))

    if source_id is None:
        source_id = get_nested(payload, "data", "params", "source_id", "value")
    if max_tx_idx is None:
        max_tx_idx = get_nested(payload, "data", "params", "max_tx_idx", "value")
    if max_tx_value is None:
        max_tx_value = get_nested(payload, "data", "params", "max_tx_value", "value")

    if source_id is None:
        raise ValueError("missing source_id")
    if max_tx_idx is None:
        raise ValueError("missing max_tx_idx")
    if max_tx_value is None:
        raise ValueError("missing max_tx_value")

    return str(source_id), int(max_tx_idx), int(max_tx_value)


def extract_payload_time(payload: dict[str, Any]) -> int:
    payload = normalize_push_payload(payload)
    params = find_params(payload)

    times = []
    for key in ("source_id", "max_tx_idx", "max_tx_value"):
        item = params.get(key)
        if isinstance(item, dict) and "time" in item:
            try:
                times.append(int(item["time"]))
            except (TypeError, ValueError):
                pass

    if times:
        return max(times)

    try:
        return int(payload.get("time", 0))
    except (TypeError, ValueError):
        return 0


def extract_optional_property(payload: dict[str, Any], key: str) -> Any:
    payload = normalize_push_payload(payload)
    params = find_params(payload)
    return unwrap_value(params.get(key))


def normalize_matrix_data(value: Any) -> list[int] | None:
    if not isinstance(value, list) or len(value) != 47:
        return None

    matrix = []
    for item in value:
        try:
            point = int(item)
        except (TypeError, ValueError):
            return None
        matrix.append(max(0, min(65535, point)))
    return matrix


def render_body(
    source_id: str,
    max_tx_idx: int,
    max_tx_value: int,
    frame_id: int | None = None,
    matrix_data: list[int] | None = None,
) -> bytes:
    body = (
        ONENET_SET_PROPERTY_BODY
        .replace("{source_id}", source_id)
        .replace("{max_tx_idx}", str(max_tx_idx))
        .replace("{max_tx_value}", str(max_tx_value))
        .replace("{target_device}", TARGET_DEVICE_NAME)
        .replace("{product_id}", ONENET_PRODUCT_ID)
    )
    data = json.loads(body)

    def normalize_numbers(value: Any) -> Any:
        if isinstance(value, dict):
            return {key: normalize_numbers(item) for key, item in value.items()}
        if isinstance(value, list):
            return [normalize_numbers(item) for item in value]
        if value == str(max_tx_idx):
            return max_tx_idx
        if value == str(max_tx_value):
            return max_tx_value
        return value

    data = normalize_numbers(data)
    params = data.setdefault("params", {})
    if frame_id is not None:
        params["frame_id"] = frame_id
    if matrix_data is not None:
        params["matrix_data"] = matrix_data

    return json.dumps(data, separators=(",", ":")).encode("utf-8")


def should_forward(source_id: str, max_tx_idx: int, max_tx_value: int, payload_time: int) -> bool:
    global last_forward_key, last_forward_ms, last_payload_time, forward_signal_active

    now_ms = int(time.monotonic() * 1000)
    key = (source_id, max_tx_idx, max_tx_value)
    min_interval = CLEAR_FORWARD_MIN_MS if max_tx_value == 0 else ACTIVE_FORWARD_MIN_MS

    with forward_lock:
        if payload_time and payload_time < START_TIME_MS - ACCEPT_PAST_MS:
            return False

        if payload_time and last_payload_time and payload_time <= last_payload_time:
            return False

        if max_tx_value == 0 and not forward_signal_active:
            return False

        if forward_signal_active and now_ms - last_forward_ms < min_interval:
            return False

        if last_forward_key == key and now_ms - last_forward_ms < min_interval:
            return False

        last_forward_key = key
        last_forward_ms = now_ms
        if payload_time:
            last_payload_time = payload_time
        forward_signal_active = max_tx_value != 0
        return True


def call_onenet(
    source_id: str,
    max_tx_idx: int,
    max_tx_value: int,
    frame_id: int | None = None,
    matrix_data: list[int] | None = None,
) -> tuple[int, str]:
    body = render_body(source_id, max_tx_idx, max_tx_value, frame_id, matrix_data)

    if not ONENET_SET_PROPERTY_URL:
        log(f"DRY-RUN set {TARGET_DEVICE_NAME}: {body.decode('utf-8')}")
        return 200, "dry-run"

    headers = {"Content-Type": "application/json"}
    if ONENET_AUTH_HEADER_VALUE:
        headers[ONENET_AUTH_HEADER_NAME] = ONENET_AUTH_HEADER_VALUE

    req = urllib.request.Request(
        ONENET_SET_PROPERTY_URL,
        data=body,
        headers=headers,
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            text = resp.read().decode("utf-8", errors="replace")
            log(f"OneNET set response status={resp.status} body={text}")
            return resp.status, text
    except urllib.error.HTTPError as exc:
        text = exc.read().decode("utf-8", errors="replace")
        log(f"OneNET set HTTPError status={exc.code} body={text}")
        return exc.code, text


class ForwardHandler(BaseHTTPRequestHandler):
    server_version = "OneNETForward/1.0"

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path != PATH:
            self.send_json(404, {"ok": False, "error": "not found"})
            return

        query = urllib.parse.parse_qs(parsed.query)
        msg = query.get("msg", [""])[0]
        nonce = query.get("nonce", [""])[0]
        signature = query.get("signature", [""])[0]

        if not msg:
            self.send_text(200, "ok")
            return

        if SHARED_TOKEN:
            expected = base64.b64encode(
                hashlib.md5(f"{SHARED_TOKEN}{nonce}{msg}".encode("utf-8")).digest()
            ).decode("utf-8")
            if urllib.parse.unquote(signature) != expected:
                log(
                    "url verification failed: "
                    f"nonce={nonce} msg={msg} signature={signature} expected={expected}"
                )
                self.send_text(403, "FAILED")
                return

        log("url verification passed")
        self.send_text(200, msg)

    def do_POST(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path != PATH:
            self.send_json(404, {"ok": False, "error": "not found"})
            return

        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length).decode("utf-8", errors="replace")
        query = urllib.parse.parse_qs(parsed.query)

        try:
            payload = json.loads(raw) if raw else {}
        except json.JSONDecodeError as exc:
            self.send_json(400, {"ok": False, "error": f"invalid json: {exc}"})
            return

        if SHARED_TOKEN and REQUIRE_POST_TOKEN and not self.token_matches(query, payload):
            self.send_json(401, {"ok": False, "error": "bad token"})
            return

        try:
            source_id, max_tx_idx, max_tx_value = extract_forward_data(payload)
            payload_time = extract_payload_time(payload)
            frame_id_raw = extract_optional_property(payload, "frame_id")
            matrix_data = normalize_matrix_data(extract_optional_property(payload, "matrix_data"))
            frame_id = int(frame_id_raw) if frame_id_raw is not None else None
            if source_id != EXPECTED_SOURCE_ID:
                self.send_json(200, {"ok": True, "ignored": f"source_id={source_id}"})
                return

            if not should_forward(source_id, max_tx_idx, max_tx_value, payload_time):
                if LOG_SKIPS:
                    log(f"skip payload: source_id={source_id} max_tx_idx={max_tx_idx} max_tx_value={max_tx_value} time={payload_time}")
                self.send_json(200, {"ok": True, "skipped": "throttled duplicate"})
                return

            log(f"push payload: {json.dumps(payload, ensure_ascii=False)}")
            status, response = call_onenet(source_id, max_tx_idx, max_tx_value, frame_id, matrix_data)
            ok = 200 <= status < 300
            self.send_json(status if ok else 502, {"ok": ok, "onenet_status": status, "response": response})
        except (BrokenPipeError, ConnectionAbortedError, ConnectionResetError, socket.timeout) as exc:
            if LOG_DISCONNECTS:
                log(f"client disconnected before response was sent: {exc}")
        except Exception as exc:  # noqa: BLE001 - small ops script, return error to caller.
            log(f"error: {exc}")
            self.send_json(400, {"ok": False, "error": str(exc)})

    def token_matches(self, query: dict[str, list[str]], payload: dict[str, Any]) -> bool:
        candidates = [
            self.headers.get("token"),
            self.headers.get("Token"),
            self.headers.get("X-Token"),
            self.headers.get("X-OneNET-Token"),
            query.get("token", [""])[0],
            str(payload.get("token", "")),
        ]
        return SHARED_TOKEN in candidates

    def send_json(self, status: int, data: dict[str, Any]) -> None:
        body = json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionAbortedError, ConnectionResetError, socket.timeout) as exc:
            if LOG_DISCONNECTS:
                log(f"client disconnected while writing json response: {exc}")

    def send_text(self, status: int, text: str) -> None:
        body = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionAbortedError, ConnectionResetError, socket.timeout) as exc:
            if LOG_DISCONNECTS:
                log(f"client disconnected while writing text response: {exc}")

    def log_message(self, fmt: str, *args: Any) -> None:
        if LOG_REQUESTS:
            log(f"{self.client_address[0]} {fmt % args}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="OneNET HTTP push forwarder")
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--port", type=int, default=PORT)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    server = ThreadingHTTPServer((args.host, args.port), ForwardHandler)
    log(f"listening on http://{args.host}:{args.port}{PATH}")
    if not ONENET_SET_PROPERTY_URL:
        log("ONENET_SET_PROPERTY_URL is not set; running in dry-run mode")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log("stopping")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
