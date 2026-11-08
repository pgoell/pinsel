"""Runs SDXL checkpoints with Hugging Face diffusers."""

import gc
from pathlib import Path
from typing import Any

import torch
from diffusers import StableDiffusionXLInpaintPipeline, StableDiffusionXLPipeline
from PIL import Image

from .protocol import Job, OutOfMemory

# SDXL base 1.0's component configs and tokenizers, without weights. With them
# a load needs no Hugging Face Hub, which from_single_file would ask otherwise.
CONFIG = Path(__file__).parent / "sdxl_config"


class DiffusersEngine:
    name = "diffusers"

    def __init__(self) -> None:
        self.device = "cuda" if torch.cuda.is_available() else "cpu"
        self.dtype = torch.float16 if self.device == "cuda" else torch.float32
        self.pipe: StableDiffusionXLPipeline | None = None

    def run(self, job: Job) -> dict[str, Any]:
        op = job.request["op"]
        handlers = {"load": self._load, "generate": self._generate, "inpaint": self._inpaint}
        if op not in handlers:
            raise ValueError(f"unknown op {op!r}")
        try:
            return handlers[op](job)
        except torch.cuda.OutOfMemoryError as error:
            message = str(error).splitlines()[0]
        # Out of the except block: its traceback holds the frames, and with them
        # the tensors, that the job had in use.
        self._free()
        raise OutOfMemory(message)

    def unload(self) -> None:
        self.pipe = None
        self._free()

    def _free(self) -> None:
        gc.collect()
        if self.device == "cuda":
            torch.cuda.empty_cache()

    def _load(self, job: Job) -> dict[str, Any]:
        # An 8 GB card holds one SDXL model, so the old one goes first.
        self.unload()
        model = job.request["model"]
        # A .ckpt is a pickle, which can run code as it loads. diffusers picks
        # the safetensors reader by this exact, case-sensitive suffix.
        if Path(model).suffix != ".safetensors":
            raise ValueError("pinsel loads only .safetensors checkpoints")
        job.progress("load", 0, 2)
        pipe = StableDiffusionXLPipeline.from_single_file(
            model, config=str(CONFIG), local_files_only=True, torch_dtype=self.dtype
        )
        job.progress("load", 1, 2)
        pipe.to(self.device)
        job.progress("load", 2, 2)
        self.pipe = pipe
        return {}

    def _generate(self, job: Job) -> dict[str, Any]:
        if self.pipe is None:
            raise RuntimeError("No model loaded")
        request = job.request
        steps = int(request.get("steps", 30))

        def step_end(_pipe, step: int, _timestep, kwargs: dict[str, Any]) -> dict[str, Any]:
            job.progress("generate", step + 1, steps)
            return kwargs

        job.progress("generate", 0, steps)
        generator = torch.Generator(self.device).manual_seed(int(request.get("seed", 0)))
        image = self.pipe(
            prompt=request["prompt"],
            negative_prompt=request.get("negative_prompt") or None,
            num_inference_steps=steps,
            width=int(request.get("width", 1024)),
            height=int(request.get("height", 1024)),
            generator=generator,
            callback_on_step_end=step_end,
        ).images[0]
        image.save(request["output"])
        return {"output": request["output"]}

    def _inpaint(self, job: Job) -> dict[str, Any]:
        if self.pipe is None:
            raise RuntimeError("No model loaded")
        request = job.request
        steps = int(request.get("steps", 30))
        strength = float(request.get("strength", 1.0))
        # diffusers runs only the last steps * strength of the schedule.
        total = max(1, min(steps, int(steps * strength)))
        with Image.open(request["image"]) as file:
            image = file.convert("RGB")
        with Image.open(request["mask"]) as file:
            mask = file.convert("L")

        def step_end(_pipe, step: int, _timestep, kwargs: dict[str, Any]) -> dict[str, Any]:
            job.progress("generate", min(step + 1, total), total)
            return kwargs

        job.progress("generate", 0, total)
        # Shares the loaded model's weights, so it costs no GPU memory of its own.
        pipe = StableDiffusionXLInpaintPipeline.from_pipe(self.pipe)
        generator = torch.Generator(self.device).manual_seed(int(request.get("seed", 0)))
        result = pipe(
            prompt=request["prompt"],
            negative_prompt=request.get("negative_prompt") or None,
            image=image,
            mask_image=mask,
            strength=strength,
            num_inference_steps=steps,
            width=image.width,
            height=image.height,
            generator=generator,
            callback_on_step_end=step_end,
        ).images[0]
        result.save(request["output"])
        return {"output": request["output"]}
