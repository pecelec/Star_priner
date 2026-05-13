"""
Minimal STAR-mode printer API for the Supabase print worker.
Includes BufferedTcpTransport for reliable raw TCP printing.
"""
from __future__ import annotations
from dataclasses import dataclass
from typing import Iterable, Optional, Protocol
import socket, time

LF=0x0A; ESC=0x1B; GS=0x1D; RS=0x1E

class Transport(Protocol):
    def write(self, data: bytes) -> None: ...
    def read(self, size: int = 1, timeout: Optional[float] = None) -> bytes: ...

class BufferedTcpTransport:
    """Buffer all writes and send one complete raw TCP job."""
    def __init__(self, host: str, port: int = 9100, timeout: float = 5.0, close_delay: float = 1.0):
        self.host=host; self.port=port; self.timeout=timeout; self.close_delay=close_delay; self.buffer=bytearray()
    def __enter__(self):
        self.buffer.clear(); return self
    def __exit__(self, exc_type, exc, tb):
        if exc_type is not None: return
        data=bytes(self.buffer)
        with socket.create_connection((self.host,self.port),timeout=self.timeout) as sock:
            sock.settimeout(self.timeout)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            sock.sendall(data)
            time.sleep(self.close_delay)
            try: sock.shutdown(socket.SHUT_WR)
            except OSError: pass
            time.sleep(0.2)
        print(f"Sent one buffered print job: {len(data)} bytes")
    def write(self,data:bytes)->None: self.buffer.extend(data)
    def read(self,size:int=1,timeout:Optional[float]=None)->bytes: return b''

@dataclass
class StarPrinter:
    transport: Transport
    encoding: str = 'cp437'
    def raw(self,data:bytes|bytearray|Iterable[int]): self.transport.write(bytes(data)); return self
    def text(self,s:str,encoding:Optional[str]=None,errors:str='replace'):
        return self.raw(s.encode(encoding or self.encoding, errors=errors))
    def write_line(self,s:str='',encoding:Optional[str]=None): self.text(s,encoding=encoding); return self.line_feed()
    def initialize(self): return self.raw([ESC,0x40])
    def two_colour_mode(self,enabled:bool=True): return self.raw([ESC,RS,0x43,1 if enabled else 0])
    def black(self): return self.raw([ESC,0x35])
    def red(self): return self.raw([ESC,0x34])
    def bold(self,enabled:bool=True): return self.raw([ESC,0x45 if enabled else 0x46])
    def double_width(self,enabled:bool=True): return self.raw([ESC,0x57,1 if enabled else 0])
    def double_height(self,enabled:bool=True): return self.raw([ESC,0x68,1 if enabled else 0])
    def underline(self,enabled:bool=True): return self.raw([ESC,0x2D,1 if enabled else 0])
    def align(self,alignment:str):
        n={'left':0,'center':1,'centre':1,'right':2}.get(alignment.lower(),0)
        return self.raw([ESC,GS,0x61,n])
    def line_feed(self): return self.raw([LF])
    def feed_lines(self,lines:int): return self.raw([ESC,0x61,max(1,min(int(lines),127))])
    def cut(self, feed:bool=True, partial:bool=True):
        mode = 3 if feed and partial else 2 if feed else 1 if partial else 0
        return self.raw([ESC,0x64,mode])
