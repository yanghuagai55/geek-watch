"""Local-only browser bridge to the persistent, real C framebuffer simulator."""
import argparse
import atexit
import json
import shutil
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

HOST = Path(__file__).resolve().parent
PAGE = r'''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MCU framebuffer preview</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#0b111b;color:#e8effa;font:15px system-ui,sans-serif}
main{max-width:920px;margin:auto;padding:32px 22px}header{display:flex;justify-content:space-between;align-items:center;gap:16px;border-bottom:1px solid #263348;padding-bottom:24px}
h1{font-size:24px;margin:0 0 7px;letter-spacing:-.7px}p{color:#95a7bf;margin:0;line-height:1.6}.tag{font-size:11px;letter-spacing:1.5px;color:#6ce4b0;margin-bottom:10px}
select,button{font:inherit;color:inherit;border:1px solid #35465d;background:#182336;border-radius:10px;padding:11px 15px}button{cursor:pointer;touch-action:none;user-select:none}button:hover,button:focus-visible{background:#263b57;border-color:#71aeff}button:active{background:#315987}
.stage{display:grid;grid-template-columns:1fr 260px;gap:46px;align-items:center;padding:38px 0}.watch{padding:18px;background:#111a28;border:1px solid #354359;border-radius:30px;box-shadow:0 18px 50px #0006;width:fit-content;margin:auto}
img{display:block;image-rendering:pixelated;background:#000;width:auto;max-width:100%;height:min(52vh,360px)}.controls{display:flex;gap:10px;justify-content:center;margin:24px 0 14px}.controls button{min-width:70px}.hint{text-align:center;font-size:12px}
.stats{margin-top:26px;border-top:1px solid #29364a}.row{display:flex;justify-content:space-between;padding:15px 0;border-bottom:1px solid #29364a;color:#9fb0c6}.row b{color:#f0f5ff;font-weight:500;font-variant-numeric:tabular-nums}h2{font-size:18px;margin:0 0 12px}footer{border-top:1px solid #263348;padding-top:20px;font-size:12px;color:#879bb7}#status{margin-top:20px;color:#6ce4b0;font-size:12px}
@media(max-width:620px){main{padding:24px 16px}header{align-items:start;flex-direction:column}.stage{grid-template-columns:1fr;gap:28px;padding-top:28px}.details{max-width:360px;width:100%;margin:auto}img{height:min(48vh,340px)}.stats{margin-top:15px}}
</style>
<main><header><div><div class="tag">LIVE C SIMULATOR</div><h1>MCU framebuffer preview</h1><p>The firmware UI, rendered by C. No browser-side UI redraw.</p></div><label>Display <select id="size" aria-label="Display resolution"><option value="128">STM32 · 128 × 160</option><option value="240">Portable · 240 × 240</option></select></label></header>
<section class="stage"><div><div class="watch"><img id="screen" alt="Live RGB565 framebuffer from the C simulator"></div><div class="controls"><button data-key="1" aria-label="Left / back">← Left</button><button data-key="2">Enter</button><button data-key="4" aria-label="Right / next">Right →</button></div><p class="hint">Arrow keys + Enter · Hold Enter to go back</p></div><div class="details"><h2>Actual simulator input</h2><p>Controls feed the C HAL key levels. Navigation, animation and pixels come from the same UI sources as the MCU.</p><div class="stats"><div class="row"><span>Framebuffer</span><b id="dimensions">—</b></div><div class="row"><span>Last frame pixels sent</span><b id="pixels">—</b></div><div class="row"><span>Dirty bands</span><b id="bands">—</b></div><div class="row"><span>UI arena bytes</span><b id="ram">—</b></div><div class="row"><span>Virtual clock</span><b id="clock">—</b></div></div><div id="status" role="status">Connecting to localhost…</div></div></section><footer>Checked-in fonts · Original UI aesthetics · RGB565 framebuffer scaled with nearest-neighbor pixels. Host timing is not MCU performance.</footer></main>
<script>
const $=id=>document.getElementById(id);let size='128',keys=0,url=null,queue=Promise.resolve();
function input(level,target=size){queue=queue.catch(()=>{}).then(async()=>{let r=await fetch('/input',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({size:target,keys:level})});if(!r.ok)throw Error(await r.text())}).catch(e=>{$('status').textContent=e.message})}
function key(bit,down){keys=down?keys|bit:keys&~bit;input(keys)}
for(const b of document.querySelectorAll('[data-key]')){b.onpointerdown=e=>{e.preventDefault();b.setPointerCapture(e.pointerId);key(+b.dataset.key,true)};b.onpointerup=b.onpointercancel=()=>key(+b.dataset.key,false);b.onlostpointercapture=()=>{if(keys&+b.dataset.key)key(+b.dataset.key,false)}}
const map={ArrowLeft:1,Enter:2,ArrowRight:4};onkeydown=e=>{if(map[e.key]&&e.target.tagName!=='SELECT'){e.preventDefault();if(!e.repeat)key(map[e.key],true)}};onkeyup=e=>{if(map[e.key]){e.preventDefault();key(map[e.key],false)}};onblur=()=>{keys=0;input(0)};document.onvisibilitychange=()=>{if(document.hidden){keys=0;input(0)}};
$('size').onchange=()=>{input(0,size);keys=0;size=$('size').value;input(0)};
async function tick(){try{let requested=size;let r=await fetch('/frame?size='+requested,{cache:'no-store'});if(!r.ok)throw Error(await r.text());let s=JSON.parse(r.headers.get('X-Frame-Stats'));let blob=await r.blob();if(requested===size){let next=URL.createObjectURL(blob);$('screen').src=next;if(url)URL.revokeObjectURL(url);url=next;$('dimensions').textContent=s.width+' × '+s.height;$('pixels').textContent=s.pixels.toLocaleString();$('bands').textContent=s.bands;$('ram').textContent=s.ram+' / '+s.budget;$('clock').textContent=(s.time_ms/1000).toFixed(2)+' s';$('status').textContent='Connected · real C framebuffer'}}catch(e){$('status').textContent='Preview unavailable: '+e.message}setTimeout(tick,100)}tick();
</script></html>'''


class Simulator:
    def __init__(self, size, folder):
        exe = HOST / ("watch_ui.exe" if size == "128" else "watch_ui_240.exe")
        if not exe.is_file():
            raise FileNotFoundError(f"Build first: {HOST / 'build.bat'}")
        # Keep build outputs unlocked while this preview session is running.
        preview_exe = folder / exe.name
        shutil.copy2(exe, preview_exe)
        self.image = folder / f"frame-{size}.bmp"
        self.lock = threading.Lock()
        self.keys = 0
        self.process = subprocess.Popen(
            [str(preview_exe), "--preview", str(self.image)], cwd=HOST.parent,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
            encoding="utf-8", bufsize=1,
        )
        self.stats = self.read_reply()
        self.last_step = time.monotonic()

    def read_reply(self):
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError("C simulator stopped; rebuild and restart the preview")
        return json.loads(line)

    def step(self, count):
        self.process.stdin.write(f"{self.keys} {count}\n")
        self.process.stdin.flush()
        self.stats = self.read_reply()

    def frame(self):
        with self.lock:
            now = time.monotonic()
            elapsed = now - self.last_step
            count = min(10, int(elapsed / 0.016))
            if count:
                self.step(count)
                self.last_step = now if elapsed > 0.16 else self.last_step + count * 0.016
            return self.image.read_bytes(), self.stats.copy()

    def set_keys(self, keys):
        with self.lock:
            self.keys = keys
            self.step(1)
            self.last_step = time.monotonic()
            return self.stats.copy()

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.close()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.process.wait(timeout=3)
        self.process.stdout.close()


class Handler(BaseHTTPRequestHandler):
    simulators = {}

    def reply(self, status, data, mime="application/json", stats=None):
        self.send_response(status)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Frame-Options", "DENY")
        if stats is not None:
            self.send_header("X-Frame-Stats", json.dumps(stats))
        self.end_headers()
        self.wfile.write(data)

    def local_request(self):
        host = self.headers.get("Host", "")
        if host not in {f"127.0.0.1:{self.server.server_port}", f"localhost:{self.server.server_port}"}:
            self.reply(403, b'{"error":"localhost only"}')
            return False
        origin = self.headers.get("Origin")
        if origin and origin not in {f"http://127.0.0.1:{self.server.server_port}",
                                     f"http://localhost:{self.server.server_port}"}:
            self.reply(403, b'{"error":"cross-origin input rejected"}')
            return False
        return True

    def do_GET(self):
        if not self.local_request():
            return
        request = urlsplit(self.path)
        try:
            if request.path == "/":
                self.reply(200, PAGE.encode(), "text/html; charset=utf-8")
            elif request.path == "/frame":
                size = parse_qs(request.query).get("size", ["128"])[0]
                if size not in self.simulators:
                    self.reply(400, b'{"error":"invalid display"}')
                    return
                image, stats = self.simulators[size].frame()
                self.reply(200, image, "image/bmp", stats)
            else:
                self.reply(404, b'{"error":"not found"}')
        except (OSError, RuntimeError, ValueError) as exc:
            self.reply(502, json.dumps({"error": str(exc)}).encode())

    def do_POST(self):
        if not self.local_request():
            return
        if self.path != "/input":
            self.reply(404, b'{"error":"not found"}')
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 512:
                raise ValueError("invalid input length")
            data = json.loads(self.rfile.read(length))
            size, keys = str(data["size"]), data["keys"]
            if size not in self.simulators or type(keys) is not int or not 0 <= keys <= 7:
                raise ValueError("invalid display or key bitmask")
            self.reply(200, json.dumps(self.simulators[size].set_keys(keys)).encode())
        except (ValueError, KeyError, TypeError) as exc:
            self.reply(400, json.dumps({"error": str(exc)}).encode())
        except (OSError, RuntimeError) as exc:
            self.reply(502, json.dumps({"error": str(exc)}).encode())

    def log_message(self, *_args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="watch-ui-preview-") as folder:
        try:
            for size in ("128", "240"):
                simulator = Simulator(size, Path(folder))
                Handler.simulators[size] = simulator
                atexit.register(simulator.close)
            server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
            print(f"Real C framebuffer preview: http://127.0.0.1:{args.port}", flush=True)
            try:
                server.serve_forever()
            except KeyboardInterrupt:
                pass
            finally:
                server.server_close()
        finally:
            for simulator in Handler.simulators.values():
                simulator.close()
                atexit.unregister(simulator.close)


if __name__ == "__main__":
    main()
