import re, os
from PIL import Image, ImageDraw, ImageFont

SRC = "dashcue-pitch-deck-slides.md"
OUT_DIR = "assets/diagrams"
os.makedirs(OUT_DIR, exist_ok=True)

with open(SRC, "r", encoding="utf-8") as f:
    text = f.read()

blocks = re.split(r"\n(?=## )", text)

diag = {}
current_slide = None

for part in blocks:
    part = part.strip()
    if not part:
        continue
    if part.startswith("## "):
        current_slide = part.replace("## ", "").strip()
        if current_slide.startswith("Rendering note"):
            current_slide = None
        continue
    if not current_slide:
        continue

    code_blocks = re.findall(r"```(mermaid|latex)?\n(.*?)```", part, flags=re.S)
    for lang, body in code_blocks:
        body = body.strip()
        if not body:
            continue
        key = f"{current_slide}".replace("/", "_").replace(" ", "_")
        safe = re.sub(r"[^a-zA-Z0-9_·-]", "_", key)
        if lang == "mermaid":
            ext = "mermaid"
        elif lang == "latex":
            ext = "latex"
        else:
            ext = "code"
        fname = f"{safe}_{ext}.png"
        path = os.path.join(OUT_DIR, fname)
        create_stub_diagram(path, current_slide, ext, body)

print("Rendered stubs:", sorted(os.listdir(OUT_DIR)))

def create_stub_diagram(path, slide_label, kind, body):
    W, H = 1600, 900
    img = Image.new("RGB", (W, H), "#161A1F")
    d = ImageDraw.Draw(img)
    try:
        mono = ImageFont.truetype("consola.ttf", 26)
        sans = ImageFont.truetype("arial.ttf", 26)
    except Exception:
        mono = ImageFont.load_default()
        sans = mono

    header = f"[{kind.upper()}] {slide_label}"
    d.text((40, 40), header, fill="#4EC9B0", font=sans)

    if kind == "mermaid":
        _draw_mermaid_stub(d, body, W, H, mono, sans)
    elif kind == "latex":
        _draw_latex_stub(d, body, W, H, mono, sans)
    else:
        _draw_code_stub(d, body, W, H, mono)

    img.save(path)

def _wrap(draw, text, font, max_w):
    words = text.split()
    lines = []
    cur = ""
    for w in words:
        trial = (cur + " " + w).strip()
        if draw.textlength(trial, font=font) <= max_w:
            cur = trial
        else:
            if cur:
                lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines

def _draw_code_stub(d, body, W, H, mono):
    maxx = W - 80
    lines = body.splitlines()
    wrapped = []
    for ln in lines:
        wrapped.extend(_wrap(d, ln if ln else " ", mono, maxx))
    y = 140
    for ln in wrapped:
        d.text((40, y), ln, fill="#D7DEE6", font=mono)
        y += 38
        if y > H - 60:
            break

def _draw_mermaid_stub(d, body, W, H, mono, sans):
    maxx = W - 120
    d.text((40, 120), "Mermaid diagram", fill="#4EC9B0", font=sans)
    d.text((40, 160), "rendered placeholder", fill="#8A949E", font=sans)
    y = 240
    for ln in body.splitlines():
        if not ln.strip():
            y += 20
            continue
        wrapped = _wrap(d, ln, mono, maxx)
        for wln in wrapped:
            d.text((60, y), wln, fill="#9AA6B2", font=mono)
            y += 40
            if y > H - 80:
                return
        y += 12

def _draw_latex_stub(d, body, W, H, mono, sans):
    d.text((40, 120), "LaTeX block", fill="#4EC9B0", font=sans)
    d.text((40, 160), "rendered placeholder", fill="#8A949E", font=sans)
    y = 240
    for ln in body.splitlines():
        if not ln.strip():
            y += 20
            continue
        wrapped = _wrap(d, ln, mono, W - 120)
        for wln in wrapped:
            d.text((60, y), wln, fill="#E0E6ED", font=mono)
            y += 42
            if y > H - 80:
                return
        y += 12
