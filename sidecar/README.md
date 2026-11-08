# pinsel sidecar

Runs SDXL checkpoints with [diffusers](https://huggingface.co/docs/diffusers) for pinsel, in a process of its own: a crash or a lack of GPU memory here ends a job, not the editor. Milestone 5 replaces it with pinsel's own pipeline, behind the same protocol.

pinsel starts it with `uv run --project sidecar --locked --no-dev --extra diffusers pinsel-sidecar`. The first start downloads torch and diffusers, which takes minutes. To run another engine, set `PINSEL_ENGINE` to its command line.

The tests need no torch: `uv run --project sidecar pytest`.

## Protocol

[JSON Lines](https://jsonlines.org): one UTF-8 JSON object per line. pinsel writes requests to the engine's stdin; the engine writes events to stdout, and anything else to stderr. pinsel keeps the end of stderr and shows its last line when the engine stops.

Every request carries a `job` id that pinsel picks. The engine runs jobs one at a time, in the order they arrive, and reads stdin while a job runs, so a cancel reaches it.

| Request | Fields |
| --- | --- |
| `load` | `model`: path to an SDXL checkpoint (`.safetensors`). Lets go of the model held before. |
| `generate` | `prompt`, `negative_prompt`, `steps`, `seed`, `width`, `height`, `output`: path for the PNG. Needs a loaded model. |
| `cancel` | Ends the job named by `job` at its next progress report, or before it starts. |

| Event | Fields |
| --- | --- |
| `ready` | `engine`: its name. Sent once, first. |
| `progress` | `job`, `stage` (`load` or `generate`), `done`, `total`. |
| `done` | `job`, and `output` for `generate`. |
| `error` | `job` (null for a line it could not read), `kind` (`cancelled`, `oom` or `failed`), `message`. |

After `oom` the engine has freed what it could and keeps running. After a failed or cancelled `load` it holds no model. If the process ends, pinsel fails every open job and starts it again for the next one, loading the last model first.

```
→ {"op": "load", "job": 1, "model": "D:/models/ponyDiffusionV6XL.safetensors"}
← {"event": "progress", "job": 1, "stage": "load", "done": 1, "total": 2}
← {"event": "done", "job": 1}
→ {"op": "generate", "job": 2, "prompt": "a fox", "steps": 30, "seed": 1, "width": 1024, "height": 1024, "output": "C:/Temp/out.png"}
← {"event": "progress", "job": 2, "stage": "generate", "done": 1, "total": 30}
→ {"op": "cancel", "job": 2}
← {"event": "error", "job": 2, "kind": "cancelled", "message": "Cancelled"}
```
