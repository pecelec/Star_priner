"""
star_dot.py

Minimal STAR-mode printer API for the Supabase print worker.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Optional, Protocol
import socket
import time

LF = 0x0A
ESC = 0x1B
FS = 0x1C
GS = 0x1D
RS = 0x1E

def normalise_for_star(text: str) -> str:
    """
    Convert common web/mobile smart punctuation into characters
    the Star printer code page can actually print.
    """
    replacements = {
        "\u2018": "'",    # left single quote
        "\u2019": "'",    # right single quote / curly apostrophe
        "\u201a": "'",    # low single quote
        "\u201b": "'",    # reversed single quote

        "\u201c": '"',    # left double quote
        "\u201d": '"',    # right double quote
        "\u201e": '"',    # low double quote

        "\u2013": "-",    # en dash
        "\u2014": "-",    # em dash
        "\u2212": "-",    # minus sign

        "\u2026": "...",  # ellipsis
        "\u00a0": " ",    # non-breaking space
    }

    text = str(text)

    for bad, good in replacements.items():
        text = text.replace(bad, good)

    return text


class Transport(Protocol):
    def write(self, data: bytes) -> None: ...
    def read(self, size: int = 1, timeout: Optional[float] = None) -> bytes: ...

class BufferedTcpTransport:
    def __init__(self, host: str, port: int = 9100, timeout: float = 5.0, close_delay: float = 1.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.close_delay = close_delay
        self.buffer = bytearray()

    def __enter__(self) -> "BufferedTcpTransport":
        self.buffer.clear()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if exc_type is not None:
            return
        data = bytes(self.buffer)
        with socket.create_connection((self.host, self.port), timeout=self.timeout) as sock:
            sock.settimeout(self.timeout)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            sock.sendall(data)
            time.sleep(self.close_delay)
            try:
                sock.shutdown(socket.SHUT_WR)
            except OSError:
                pass
            time.sleep(0.2)
        print(f"Sent one buffered print job: {len(data)} bytes")

    def write(self, data: bytes) -> None:
        self.buffer.extend(data)

    def read(self, size: int = 1, timeout: Optional[float] = None) -> bytes:
        return b""

@dataclass
class StarPrinter:
    transport: Transport
    encoding: str = "cp437"

    def raw(self, data: bytes | bytearray | Iterable[int]) -> "StarPrinter":
        self.transport.write(bytes(data))
        return self

    def text(self, s: str, encoding: Optional[str] = None, errors: str = "replace") -> "StarPrinter":
        enc = encoding or self.encoding
        s = normalise_for_star(s)
        return self.raw(s.encode(enc, errors=errors))

    def write_line(self, s: str = "", encoding: Optional[str] = None) -> "StarPrinter":
        self.text(s, encoding=encoding)
        return self.line_feed()

    def initialize(self) -> "StarPrinter":
        return self.raw([ESC, 0x40])

    def two_colour_mode(self, enabled: bool = True) -> "StarPrinter":
        return self.raw([ESC, RS, 0x43, 1 if enabled else 0])

    def print_logo(self, logo_number: int = 1, mode: int = 0) -> "StarPrinter":
        """
        ESC FS p n m
        Print registered logo. logo_number=1 is the first saved logo.
        """
        logo_number = int(logo_number)
        mode = int(mode)
        if not 1 <= logo_number <= 255:
            raise ValueError("logo_number must be 1..255")
        if mode not in (0, 1, 2, 3, 48, 49, 50, 51):
            raise ValueError("logo mode must be 0..3")
        return self.raw([ESC, FS, 0x70, logo_number, mode])

    def black(self) -> "StarPrinter":
        return self.raw([ESC, 0x35])

    def red(self) -> "StarPrinter":
        return self.raw([ESC, 0x34])

    def bold(self, enabled: bool = True) -> "StarPrinter":
        return self.raw([ESC, 0x45 if enabled else 0x46])

    def double_width(self, enabled: bool = True) -> "StarPrinter":
        return self.raw([ESC, 0x57, 1 if enabled else 0])

    def double_height(self, enabled: bool = True) -> "StarPrinter":
        return self.raw([ESC, 0x68, 1 if enabled else 0])

    def underline(self, enabled: bool = True) -> "StarPrinter":
        return self.raw([ESC, 0x2D, 1 if enabled else 0])

    def align(self, alignment: str) -> "StarPrinter":
        mapping = {"left": 0, "center": 1, "centre": 1, "right": 2}
        return self.raw([ESC, GS, 0x61, mapping.get(alignment.lower(), 0)])

    def line_feed(self) -> "StarPrinter":
        return self.raw([LF])

    def feed_lines(self, lines: int) -> "StarPrinter":
        lines = max(1, min(int(lines), 127))
        return self.raw([ESC, 0x61, lines])

    def cut(self, feed: bool = True, partial: bool = True) -> "StarPrinter":
        if feed and partial:
            mode = 3
        elif feed and not partial:
            mode = 2
        elif not feed and partial:
            mode = 1
        else:
            mode = 0
        return self.raw([ESC, 0x64, mode])

        def normalise_for_star(self, text: str) -> str:
            """
            Convert common web/mobile smart punctuation into characters
            the Star printer code page can actually print.
            """
            replacements = {
                "\u2018": "'",    # left single quote
                "\u2019": "'",    # right single quote / curly apostrophe
                "\u201a": "'",    # low single quote
                "\u201b": "'",    # reversed single quote

                "\u201c": '"',    # left double quote
                "\u201d": '"',    # right double quote
                "\u201e": '"',    # low double quote

                "\u2013": "-",    # en dash
                "\u2014": "-",    # em dash
                "\u2212": "-",    # minus sign

                "\u2026": "...",  # ellipsis
                "\u00a0": " ",    # non-breaking space
            }

            text = str(text)

            for bad, good in replacements.items():
                text = text.replace(bad, good)

            return text