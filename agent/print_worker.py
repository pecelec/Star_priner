"""
print_worker.py

Central Mac/PC worker for the Supabase print queue.
Prints the first registered logo before each message.
"""

from __future__ import annotations

import os
import time
import traceback
import textwrap
from datetime import datetime, timezone
from typing import Any

from dotenv import load_dotenv
from supabase import create_client, Client

from star_dot import StarPrinter, BufferedTcpTransport

load_dotenv()

SUPABASE_URL = os.environ["SUPABASE_URL"]
SUPABASE_SERVICE_ROLE_KEY = os.environ["SUPABASE_SERVICE_ROLE_KEY"]

PRINTER_IP = os.getenv("PRINTER_IP", "192.168.178.121")
PRINTER_PORT = int(os.getenv("PRINTER_PORT", "9100"))
POLL_INTERVAL_SECONDS = float(os.getenv("POLL_INTERVAL_SECONDS", "1.0"))
TCP_CLOSE_DELAY_SECONDS = float(os.getenv("TCP_CLOSE_DELAY_SECONDS", "1.0"))
WORKER_NAME = os.getenv("WORKER_NAME", "central-printer-worker")
PRINT_LINE_WIDTH = int(os.getenv("PRINT_LINE_WIDTH", "32"))

supabase: Client = create_client(SUPABASE_URL, SUPABASE_SERVICE_ROLE_KEY)


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def fetch_next_pending_job() -> dict[str, Any] | None:
    response = (
        supabase.table("print_jobs")
        .select("*")
        .eq("status", "pending")
        .order("created_at")
        .limit(1)
        .execute()
    )
    if not response.data:
        return None
    return response.data[0]


def update_job(job_id: str, values: dict[str, Any]) -> None:
    values["updated_at"] = utc_now_iso()
    supabase.table("print_jobs").update(values).eq("id", job_id).execute()


def wrapped_lines(text: str, width: int = PRINT_LINE_WIDTH) -> list[str]:
    result: list[str] = []
    for paragraph in str(text).replace("\r\n", "\n").split("\n"):
        paragraph = paragraph.rstrip()
        if not paragraph:
            result.append("")
            continue
        result.extend(
            textwrap.wrap(
                paragraph,
                width=width,
                replace_whitespace=False,
                drop_whitespace=True,
            )
        )
    return result


def print_payload(payload: dict[str, Any]) -> None:
    title = payload.get("title")
    lines = payload.get("lines", [])
    two_colour = bool(payload.get("two_colour", True))
    feed_lines = int(payload.get("feed_lines", 3))
    cut = bool(payload.get("cut", True))
    partial_cut = bool(payload.get("partial_cut", True))

    print_logo = bool(payload.get("print_logo", True))
    logo_number = int(payload.get("logo_number", 1))
    logo_mode = int(payload.get("logo_mode", 0))

    with BufferedTcpTransport(
        PRINTER_IP,
        PRINTER_PORT,
        close_delay=TCP_CLOSE_DELAY_SECONDS,
    ) as transport:
        p = StarPrinter(transport)
        p.initialize()

        if two_colour:
            p.two_colour_mode(True)

        if print_logo:
            p.align("center")
            p.print_logo(logo_number, logo_mode)
            p.write_line()
            p.align("left")

        if title:
            p.align("center")
            p.bold(True)
            p.double_width(True)
            p.black()
            p.write_line(str(title))
            p.double_width(False)
            p.bold(False)
            p.align("left")
            p.write_line()

        for line in lines:
            raw_text = str(line.get("text", ""))
            colour = str(line.get("colour", "black")).lower()
            bold = bool(line.get("bold", False))
            double_width = bool(line.get("double_width", False))
            double_height = bool(line.get("double_height", False))
            underline = bool(line.get("underline", False))
            align = str(line.get("align", "left"))

            p.align(align)
            p.bold(bold)
            p.double_width(double_width)
            p.double_height(double_height)
            p.underline(underline)
            p.red() if colour == "red" else p.black()

            width = max(8, PRINT_LINE_WIDTH // 2) if double_width else PRINT_LINE_WIDTH
            for printable_line in wrapped_lines(raw_text, width=width):
                p.write_line(printable_line)

            p.bold(False)
            p.double_width(False)
            p.double_height(False)
            p.underline(False)
            p.black()

        p.align("left")
        p.black()

        if feed_lines > 0:
            p.feed_lines(feed_lines)
        if cut:
            p.cut(feed=True, partial=partial_cut)


def process_one_job() -> bool:
    job = fetch_next_pending_job()
    if job is None:
        return False

    job_id = job["id"]
    attempts = int(job.get("attempts") or 0)
    print(f"Claiming job {job_id}")

    update_job(
        job_id,
        {
            "status": "printing",
            "claimed_at": utc_now_iso(),
            "attempts": attempts + 1,
            "worker_name": WORKER_NAME,
            "error": None,
        },
    )

    try:
        payload = job["payload"]
        if not isinstance(payload, dict):
            raise ValueError("Job payload must be a JSON object")
        print_payload(payload)
        update_job(job_id, {"status": "done", "printed_at": utc_now_iso(), "error": None})
        print(f"Done job {job_id}")
        return True
    except Exception as exc:
        error_text = "".join(traceback.format_exception_only(type(exc), exc)).strip()
        print(f"FAILED job {job_id}: {error_text}")
        update_job(job_id, {"status": "failed", "error": error_text})
        return True


def main() -> None:
    print("Star print worker started")
    print(f"Worker: {WORKER_NAME}")
    print(f"Printer: {PRINTER_IP}:{PRINTER_PORT}")
    print(f"Polling every {POLL_INTERVAL_SECONDS}s")
    while True:
        did_work = process_one_job()
        if not did_work:
            time.sleep(POLL_INTERVAL_SECONDS)


if __name__ == "__main__":
    main()
