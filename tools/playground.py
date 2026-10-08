#!/usr/bin/env python3
"""playground.py — เซิร์ฟเวอร์เล็ก ๆ สำหรับหน้า docs/playground.html

รับโค้ด C จากหน้าเว็บ -> compile ด้วย gcc -g -> รันใต้ LD_PRELOAD -> ส่งผลกลับไปแสดง

===================== อ่านก่อนใช้ =====================
เซิร์ฟเวอร์นี้ compile แล้วรันโค้ด C ที่รับมาจากหน้าเว็บ
ใครที่ต่อเข้าพอร์ตนี้ได้ เท่ากับรันอะไรก็ได้บนเครื่องนี้

จึงออกแบบให้ใช้เฉพาะตอน demo บนเครื่องตัวเองเท่านั้น
  - ผูกกับ 127.0.0.1 เท่านั้น เครื่องอื่นในวงแลนต่อไม่ได้
  - จำกัดขนาดโค้ดและเวลารัน
ห้ามเปิดออกอินเทอร์เน็ต ห้ามรันบนเครื่องที่ใช้ร่วมกับคนอื่น
======================================================

วิธีใช้
    make playground          หรือ      python3 tools/playground.py
แล้วเปิด http://127.0.0.1:8000 ในเบราว์เซอร์
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
LIB = ROOT / "build" / "libdetect.so"

MAX_CODE_BYTES = 64 * 1024
MIN_TIMEOUT = 1
MAX_TIMEOUT = 15
COMPILE_TIMEOUT = 60

CTYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".svg": "image/svg+xml",
    ".png": "image/png",
}

# ---- ตัวอ่านรายงานจาก stderr ให้เป็นโครงสร้างที่หน้าเว็บเอาไปวาดภาพต่อได้ ----
RE_HEAD = re.compile(r"\[DEADLOCK DETECTED\]\s+(\d+)\s+threads?\s+in\s+cycle")
RE_THREAD = re.compile(r"^\s*Thread\s+(\d+)\s*$")
RE_SITE = re.compile(r"^\s*(holds|waiting)\s+mutex\s+(\S+)\s+at\s+(.+?)\s*$")
RE_SITE_BARE = re.compile(r"^\s*(holds|waiting)\s+mutex\s+(\S+)\s*$")
RE_CYCLE = re.compile(r"^\s*Cycle:\s*(.+?)\s*$")


def parse_report(text):
    """คืน dict ของ cycle ที่เจอ หรือ None ถ้าไม่มีรายงาน"""
    report = None
    cur = None
    for line in text.splitlines():
        head = RE_HEAD.search(line)
        if head:
            report = {"count": int(head.group(1)), "threads": [], "cycle": []}
            cur = None
            continue
        if report is None:
            continue
        m = RE_THREAD.match(line)
        if m:
            cur = {"tid": m.group(1), "holds": None, "waiting": None}
            report["threads"].append(cur)
            continue
        m = RE_SITE.match(line)
        if m and cur is not None:
            cur[m.group(1)] = {"mutex": m.group(2), "site": m.group(3)}
            continue
        m = RE_SITE_BARE.match(line)
        if m and cur is not None:
            cur[m.group(1)] = {"mutex": m.group(2), "site": ""}
            continue
        m = RE_CYCLE.match(line)
        if m:
            report["cycle"] = [t.strip() for t in m.group(1).split("->")]
    return report


def as_text(value):
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return value


# ต้องขึ้นต้นด้วยตัวอักษร/ตัวเลข/_/- (ห้ามขึ้นต้นด้วยจุด) และลงท้ายด้วย .c
RE_SAFE_NAME = re.compile(r"[A-Za-z0-9_-][A-Za-z0-9._-]{0,61}\.c\Z")


def safe_filename(name):
    """ใช้ชื่อไฟล์ที่ผู้ใช้แนบมา เพื่อให้รายงานชี้เป็น bank.c:46 แทน prog.c:46

    ชื่อนี้ถูกเอาไปสร้างไฟล์จริงในโฟลเดอร์ชั่วคราว จึง
      - ตัด path ทิ้งด้วย basename  กันไม่ให้หลุดออกนอกโฟลเดอร์
      - รับเฉพาะอักขระปลอดภัยและต้องมีชื่อจริงนำหน้า .c
    อะไรที่ไม่เข้าเกณฑ์ก็ถอยไปใช้ prog.c

    (ไม่ได้พึ่ง quoting ของ shell เพราะเราเรียก gcc ผ่าน list ไม่ผ่าน shell อยู่แล้ว
     กฎนี้มีไว้กัน path traversal กับกันชื่อไฟล์ประหลาดที่ทำให้รายงานอ่านไม่รู้เรื่อง)
    """
    name = os.path.basename(str(name or "")).strip()
    if not RE_SAFE_NAME.match(name):
        return "prog.c"
    return name


def run_code(code, mode, timeout, filename="prog.c"):
    out = {
        "stage": "compile",
        "compile_ok": False,
        "compile_output": "",
        "stdout": "",
        "stderr": "",
        "exit": None,
        "timed_out": False,
        "report": None,
        "dot": "",
        "mode": mode,
        "timeout": timeout,
    }

    if shutil.which("gcc") is None:
        out["compile_output"] = "ไม่พบคำสั่ง gcc — ติดตั้งด้วย sudo apt install build-essential"
        return out
    if mode == "detect" and not LIB.is_file():
        out["compile_output"] = "ยังไม่มี build/libdetect.so — สั่ง make ก่อน"
        return out

    with tempfile.TemporaryDirectory(prefix="dd-play-") as td:
        tdp = Path(td)
        src = tdp / safe_filename(filename)
        exe = tdp / "prog"
        dot = tdp / "cycle.dot"
        out["filename"] = src.name
        src.write_text(code, encoding="utf-8")

        try:
            cp = subprocess.run(
                ["gcc", "-std=c11", "-Wall", "-Wextra", "-g", "-O0", "-pthread",
                 "-o", str(exe), str(src)],
                capture_output=True, text=True, timeout=COMPILE_TIMEOUT)
        except subprocess.TimeoutExpired:
            out["compile_output"] = "compile นานเกินไป"
            return out

        out["compile_output"] = cp.stderr.strip()
        if cp.returncode != 0:
            return out
        out["compile_ok"] = True
        out["stage"] = "run"

        env = dict(os.environ)
        env.pop("LD_PRELOAD", None)
        env.pop("DD_DOT_OUT", None)
        env.pop("DD_VERBOSE", None)
        if mode == "detect":
            env["LD_PRELOAD"] = str(LIB)
            env["DD_DOT_OUT"] = str(dot)

        try:
            rp = subprocess.run([str(exe)], capture_output=True, text=True,
                                timeout=timeout, env=env, cwd=td)
            out["exit"] = rp.returncode
            out["stdout"] = rp.stdout
            out["stderr"] = rp.stderr
        except subprocess.TimeoutExpired as e:
            # โปรแกรมค้าง = สิ่งที่เราอยากเห็นพอดี เก็บ output เท่าที่ออกมาแล้ว
            out["timed_out"] = True
            out["stdout"] = as_text(e.stdout)
            out["stderr"] = as_text(e.stderr)

        if dot.is_file():
            out["dot"] = dot.read_text(encoding="utf-8", errors="replace")
        out["report"] = parse_report(out["stderr"])

    return out


RE_INTERVAL = re.compile(r"#define\s+DD_CHECK_INTERVAL_MS\s+(\d+)")


def detector_interval_ms():
    """อ่านคาบการตรวจจาก src/common.h จะได้ไม่ต้องมาแก้สองที่เวลาเปลี่ยนค่า"""
    try:
        text = (ROOT / "src" / "common.h").read_text(encoding="utf-8", errors="replace")
        m = RE_INTERVAL.search(text)
        if m:
            return int(m.group(1))
    except OSError:
        pass
    return 500


def server_status():
    """ข้อมูลไว้โชว์แถบสถานะบนหน้าเว็บ ให้รู้ว่าเครื่องมือพร้อมใช้จริงไหม"""
    gcc = shutil.which("gcc")
    version = ""
    if gcc:
        try:
            cp = subprocess.run([gcc, "-dumpfullversion"], capture_output=True,
                                text=True, timeout=10)
            version = cp.stdout.strip()
        except (OSError, subprocess.SubprocessError):
            version = ""
    return {
        "lib_ready": LIB.is_file(),
        "lib_path": str(LIB),
        "gcc": bool(gcc),
        "gcc_version": version,
        "addr2line": bool(shutil.which("addr2line")),
        "interval_ms": detector_interval_ms(),
        "max_timeout": MAX_TIMEOUT,
    }


class Handler(BaseHTTPRequestHandler):
    server_version = "DeadlockPlayground/1.0"

    def log_message(self, fmt, *args):
        sys.stderr.write("  " + (fmt % args) + "\n")

    def send_body(self, code, body, ctype="application/json; charset=utf-8"):
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def send_json(self, code, obj):
        self.send_body(code, json.dumps(obj, ensure_ascii=False))

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/api/status":
            self.send_json(200, server_status())
            return
        if path in ("/", "/index.html"):
            path = "/playground.html"
        target = DOCS / os.path.basename(path)   # basename กัน path traversal
        if target.is_file() and target.suffix in CTYPES:
            self.send_body(200, target.read_bytes(), CTYPES[target.suffix])
        else:
            self.send_body(404, b"not found", "text/plain; charset=utf-8")

    def do_POST(self):
        if self.path.split("?", 1)[0] != "/api/run":
            self.send_json(404, {"error": "ไม่มี endpoint นี้"})
            return
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            length = 0
        if length <= 0:
            self.send_json(400, {"error": "ไม่มีข้อมูลส่งมา"})
            return
        if length > MAX_CODE_BYTES:
            self.send_json(400, {"error": "โค้ดยาวเกิน 64KB"})
            return
        try:
            req = json.loads(self.rfile.read(length).decode("utf-8"))
        except Exception:
            self.send_json(400, {"error": "อ่าน JSON ไม่ได้"})
            return

        code = req.get("code") or ""
        if not code.strip():
            self.send_json(400, {"error": "ยังไม่ได้ใส่โค้ด"})
            return
        mode = "detect" if req.get("mode") == "detect" else "plain"
        try:
            timeout = int(req.get("timeout") or 5)
        except (TypeError, ValueError):
            timeout = 5
        timeout = max(MIN_TIMEOUT, min(MAX_TIMEOUT, timeout))
        filename = req.get("filename") or "prog.c"

        try:
            self.send_json(200, run_code(code, mode, timeout, filename))
        except Exception as exc:
            self.send_json(500, {"error": "server พัง: " + str(exc)})


def main():
    ap = argparse.ArgumentParser(description="เซิร์ฟเวอร์ demo สำหรับ deadlock detector")
    ap.add_argument("--port", type=int, default=8000)
    args = ap.parse_args()

    lib_state = "พร้อม" if LIB.is_file() else "ยังไม่มี -> สั่ง make ก่อน"
    gcc_path = shutil.which("gcc") or "ไม่พบ -> sudo apt install build-essential"
    bar = "=" * 62

    print(bar)
    print("  Deadlock Detector — playground")
    print(bar)
    print("  โปรเจ็ค : " + str(ROOT))
    print("  library : " + str(LIB) + "  " + lib_state)
    print("  gcc     : " + gcc_path)
    print("")
    print("  เปิดเบราว์เซอร์ไปที่  http://127.0.0.1:" + str(args.port))
    print("  กด Ctrl-C เพื่อหยุด")
    print("")
    print("  ความปลอดภัย: เซิร์ฟเวอร์นี้ compile และรันโค้ด C ที่ส่งมาจากหน้าเว็บ")
    print("  จึงผูกไว้กับ 127.0.0.1 เท่านั้น ห้ามเปิดออกเน็ต")
    print(bar)

    srv = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("")
        print("  หยุดแล้ว")
    finally:
        srv.server_close()


if __name__ == "__main__":
    main()
