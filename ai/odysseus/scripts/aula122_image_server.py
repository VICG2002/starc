#!/usr/bin/env python3
"""aula122_image_server.py — servidor de imágenes local, MPS-native (Apple Metal),
para Odiseo / Aula 122.

OpenAI-compatible: /v1/images/generations, /v1/models, /health — lo que odysseus
espera de un endpoint `model_type=image` (generate_image / mood board / storyboard).

A diferencia de scripts/diffusion_server.py (que es solo-CUDA y no carga en Mac),
este detecta el device (mps > cuda > cpu) y carga el modelo de difusión de forma
LAZY: el pipeline se carga al PRIMER request, no al arrancar. Así no ocupa RAM
mientras no se generan imágenes — clave en Macs de 18 GB junto al LLM 14b.

Uso:
    python3 aula122_image_server.py --model stabilityai/sd-turbo --port 8771
"""
import argparse
import base64
import io
import logging
import threading

import torch
import uvicorn
from fastapi import FastAPI
from pydantic import BaseModel

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("aula122-image")

_DTYPES = {"float16": torch.float16, "bfloat16": torch.bfloat16, "float32": torch.float32}


def _pick_device() -> str:
    if torch.backends.mps.is_available():
        return "mps"
    if torch.cuda.is_available():
        return "cuda"
    return "cpu"


DEVICE = _pick_device()

_args = None
_pipe = None
_lock = threading.Lock()


def get_pipe():
    """Carga el pipeline una sola vez (lazy + thread-safe)."""
    global _pipe
    if _pipe is None:
        with _lock:
            if _pipe is None:
                from diffusers import AutoPipelineForText2Image

                dtype = _DTYPES.get(_args.dtype, torch.float16)
                if DEVICE == "mps":
                    # MPS + float16 produce NaN en el decode del VAE (imágenes
                    # negras, flaky). float32 es estable en Metal; SD-Turbo es chico
                    # y rápido (pocos pasos) aun en fp32.
                    dtype = torch.float32
                log.info("Cargando %s en %s (dtype=%s)…", _args.model, DEVICE, str(dtype))
                pipe = AutoPipelineForText2Image.from_pretrained(
                    _args.model, torch_dtype=dtype, safety_checker=None
                )
                pipe = pipe.to(DEVICE)
                # SIN attention/vae slicing: sobre MPS + float16 el VAE slicing
                # produce NaN (imágenes negras). SD-Turbo es chico y no lo necesita.
                # Si en el futuro persistieran negros, el fix estándar es subir el
                # VAE a float32: pipe.vae = pipe.vae.to(torch.float32).
                _pipe = pipe
                log.info("Modelo de imagen cargado.")
    return _pipe


app = FastAPI(title="Aula 122 Image Server")


class ImageRequest(BaseModel):
    model: str = ""
    prompt: str
    n: int = 1
    size: str = "512x512"
    quality: str = "medium"
    response_format: str = "b64_json"


@app.get("/health")
def health():
    return {"status": "ok", "device": DEVICE,
            "model": (_args.model if _args else None),
            "loaded": _pipe is not None}


@app.get("/v1/models")
def list_models():
    mid = (_args.model.split("/")[-1] if _args else "image")
    return {"object": "list", "data": [{"id": mid, "object": "model"}]}


@app.post("/v1/images/generations")
def generate(req: ImageRequest):
    pipe = get_pipe()
    try:
        w, h = (int(v) for v in req.size.lower().split("x"))
    except Exception:
        w = h = 512
    # SD-Turbo: pocos pasos, sin guidance. Mapeo de "quality" a pasos.
    steps = {"low": 1, "medium": 3, "high": 6}.get(req.quality, 3)
    if _args and _args.steps:
        steps = _args.steps
    data = []
    for _ in range(max(1, int(req.n))):
        image = pipe(prompt=req.prompt, num_inference_steps=steps,
                     guidance_scale=0.0, width=w, height=h).images[0]
        buf = io.BytesIO()
        image.save(buf, format="PNG")
        data.append({"b64_json": base64.b64encode(buf.getvalue()).decode("ascii")})
    return {"created": 0, "data": data}


def main():
    global _args
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, help="Repo HF o ruta local del modelo de difusión")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8771)
    ap.add_argument("--dtype", default="float16", choices=["float16", "bfloat16", "float32"])
    ap.add_argument("--width", type=int, default=512)
    ap.add_argument("--height", type=int, default=512)
    ap.add_argument("--steps", type=int, default=0, help="Pasos fijos (0=auto por quality)")
    # Aceptados por compatibilidad con diffusion_server.py (el slicing ya se aplica):
    ap.add_argument("--attention-slicing", action="store_true")
    ap.add_argument("--vae-slicing", action="store_true")
    _args = ap.parse_args()
    log.info("Aula 122 image server — device=%s, model=%s, puerto=%s", DEVICE, _args.model, _args.port)
    uvicorn.run(app, host=_args.host, port=_args.port, log_level="warning")


if __name__ == "__main__":
    main()
