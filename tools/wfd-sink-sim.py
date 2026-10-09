#!/usr/bin/env python3
"""Senken-Attrappe: spielt den Miracast-Empfaenger, damit sich der
RTSP-Handschlag der Quelle ohne Dongle pruefen laesst.

Die Attrappe macht genau das, was ein Dongle tut, nachdem die Wi-Fi-Direct-
Gruppe steht -- nur eben ueber das gewoehnliche Netz:

    sie verbindet sich zu Quelle:7236, beantwortet M1, schickt M2, fragt
    mit M3 die Faehigkeiten ab, nimmt M4/M5 entgegen, schickt SETUP (M6)
    und PLAY (M7), nimmt den RTP-Strom entgegen und beendet mit TEARDOWN.

    tools/wfd-sink-sim.py <quelle-ip> [sekunden] [ausgabe.ts]

Der empfangene Strom wird als MPEG-TS geschrieben (RTP-Kopf abgeschnitten),
also direkt abspielbar.
"""
import re
import socket
import sys
import threading
import time

SRC = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.8"
SECONDS = int(sys.argv[2]) if len(sys.argv) > 2 else 20
OUT = sys.argv[3] if len(sys.argv) > 3 else "/tmp/sink.ts"
RTP_PORT = 19100


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


class Sink:
    def __init__(self, conn):
        self.c = conn
        self.buf = b""
        self.cseq = 100
        self.session = None

    def recv(self):
        while b"\r\n\r\n" not in self.buf:
            d = self.c.recv(4096)
            if not d:
                return None
            self.buf += d
        head, self.buf = self.buf.split(b"\r\n\r\n", 1)
        head = head.decode(errors="replace")
        body = ""
        m = re.search(r"Content-Length:\s*(\d+)", head, re.I)
        if m:
            n = int(m.group(1))
            while len(self.buf) < n:
                self.buf += self.c.recv(4096)
            body, self.buf = self.buf[:n].decode(errors="replace"), self.buf[n:]
        log("<<<", head.split("\r\n")[0], "|", body.replace("\r\n", " | ")[:200])
        return head, body

    def reply(self, cseq, headers=None, body=""):
        msg = "RTSP/1.0 200 OK\r\nCSeq: %s\r\n" % cseq
        for k, v in (headers or {}).items():
            msg += "%s: %s\r\n" % (k, v)
        if body:
            msg += "Content-Type: text/parameters\r\nContent-Length: %d\r\n" % len(body)
        msg += "\r\n" + body
        log(">>> 200 OK", (body or "").replace("\r\n", " | ")[:160])
        self.c.sendall(msg.encode())

    def request(self, method, uri, headers=None, body=""):
        self.cseq += 1
        msg = "%s %s RTSP/1.0\r\nCSeq: %d\r\n" % (method, uri, self.cseq)
        for k, v in (headers or {}).items():
            msg += "%s: %s\r\n" % (k, v)
        if body:
            msg += "Content-Type: text/parameters\r\nContent-Length: %d\r\n" % len(body)
        msg += "\r\n" + body
        log(">>>", method, uri)
        self.c.sendall(msg.encode())


def rtp_receiver(stop, path):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    s.bind(("0.0.0.0", RTP_PORT))
    s.settimeout(1.0)
    n = total = 0
    with open(path, "wb") as f:
        while not stop.is_set():
            try:
                d, _ = s.recvfrom(2048)
            except socket.timeout:
                continue
            if len(d) > 12:
                f.write(d[12:])     # RTP-Kopf weg, TS-Pakete bleiben
                n += 1
                total += len(d) - 12
    log("== RTP: %d Pakete, %d Byte -> %s" % (n, total, path))


def main():
    log("== verbinde zu %s:7236" % SRC)
    c = socket.create_connection((SRC, 7236), timeout=30)
    c.settimeout(20)
    sink = Sink(c)
    stop = threading.Event()
    rx = threading.Thread(target=rtp_receiver, args=(stop, OUT), daemon=True)
    rx.start()

    state = "wait_m1"
    t_play = None
    while True:
        if t_play and time.time() - t_play > SECONDS:
            sink.request("TEARDOWN", "rtsp://localhost/wfd1.0/streamid=0",
                         {"Session": sink.session or "1"})
            break
        try:
            msg = sink.recv()
        except socket.timeout:
            if state == "playing":
                continue
            log("!! nichts mehr von der Quelle (Zustand %s)" % state)
            break
        if msg is None:
            log("!! Quelle hat aufgelegt (Zustand %s)" % state)
            break
        head, body = msg
        first = head.split("\r\n")[0]
        cseq = (re.search(r"CSeq:\s*(\d+)", head, re.I) or [None, "0"])[1]

        if first.startswith("RTSP/1.0"):
            if state == "m2sent":
                state = "wait_m3"
            elif state == "m6sent":
                m = re.search(r"Session:\s*([^;\r\n]+)", head, re.I)
                if m:
                    sink.session = m.group(1).strip()
                log("== Sitzung:", sink.session)
                state = "playing"
                sink.request("PLAY", "rtsp://localhost/wfd1.0/streamid=0",
                             {"Session": sink.session})
                t_play = time.time()
            continue

        method = first.split(" ")[0]
        if method == "OPTIONS":                       # M1
            sink.reply(cseq, {"Public": "org.wfa.wfd1.0, GET_PARAMETER, "
                                        "SET_PARAMETER"})
            if state == "wait_m1":
                state = "m2sent"
                sink.request("OPTIONS", "*", {"Require": "org.wfa.wfd1.0"})
        elif method == "GET_PARAMETER":               # M3
            caps = ("wfd_video_formats: 00 00 02 02 00000003 00000000 00000000"
                    " 00 0000 0000 00 none none\r\n"
                    "wfd_audio_codecs: LPCM 00000003 00, AAC 00000001 00\r\n"
                    "wfd_client_rtp_ports: RTP/AVP/UDP;unicast %d 0 mode=play\r\n"
                    "wfd_content_protection: none\r\n" % RTP_PORT)
            sink.reply(cseq, {}, caps)
            state = "wait_m4"
        elif method == "SET_PARAMETER":               # M4 und M5
            sink.reply(cseq)
            if "wfd_trigger_method: SETUP" in body:   # M5 -> wir machen M6
                state = "m6sent"
                sink.request("SETUP", "rtsp://localhost/wfd1.0/streamid=0",
                             {"Transport": "RTP/AVP/UDP;unicast;client_port=%d"
                                           % RTP_PORT})
        else:
            sink.reply(cseq)

    stop.set()
    time.sleep(1.5)
    c.close()
    log("== Ende")


if __name__ == "__main__":
    main()
