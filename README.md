# pinsel

Native image editor for generating images and local edits, built by hand down to its own GPU kernels.

Paint a mask, let go of the brush, and see the edit at once. Generation and local edits that feel like Photoshop or Krita, not like a web form. A learning project too: I write the core myself, from the GPU canvas down to the CUDA kernels.

## Scope

- **In:** GPU canvas, brush, lasso and mask, undo, layers for results, blend modes, inpainting on a region, live preview, LoRAs, export.
- **Out:** everything else a paint program does (brush engines, color management, PSD, filters). Krita has it.

## Milestones

1. GPU canvas: pan and zoom a 4K image at 60 fps or more.
2. Brush, mask and undo, on tiles.
3. Layers and blend modes in shaders.
4. Inference through a Python sidecar: crop-to-region inpainting end to end.
5. Own SDXL pipeline in C++ and CUDA.
6. Live preview with a few-step model, and my own kernels where the profiler points.

First target: SDXL-family models (Pony V6 XL, Illustrious, NoobAI) on an RTX 3060 Ti (Ampere, 8 GB).

## Setup

Needs [mise](https://mise.jdx.dev).

```sh
mise install      # cmake, ninja, clang-format, clang-tidy, lefthook, cocogitto, jq, aqtinstall
mise run install-qt  # Qt into .qt/; set CMAKE_PREFIX_PATH (below) to the folder it prints
mise run install  # git hooks + configure (fetches GoogleTest)
mise run test     # Debug build with sanitizers, then ctest
mise run lint     # clang-format + clang-tidy
```

The compiler comes from the system: GCC 13 or Clang 17 or newer, with C++20.

Qt 6.10 or newer, with the Qt SVG module, comes from `mise run install-qt` or Qt's own installer. Point CMake at it in `mise.local.toml`, which git ignores:

```toml
[env]
CMAKE_PREFIX_PATH = "<the folder install-qt printed>"
```

On Linux, Qt also needs the OpenGL headers (`libgl-dev` on Ubuntu). On Windows, put Qt's `bin` folder on `PATH` so the tests find its DLLs.

Windows is the main platform, and CI runs the tests there only. `lint`, `fmt` and `check-commits` run in bash, so put Git for Windows' `bin` folder on `PATH` ahead of `C:\Windows\System32`, where WSL's `bash.exe` lives; `where bash` must list Git's copy first. Build with Clang (`$env:CXX = 'clang++'` in PowerShell, `set CXX=clang++` in cmd, before `mise run install`) from LLVM or Visual Studio's Clang tools. The tests need AddressSanitizer's runtime DLL on `PATH`: add the folder that `clang++ -print-resource-dir` prints, plus `\lib\windows`. On Windows, UndefinedBehaviorSanitizer runs in trap mode: a fault fails the test with SEH exception 0xc000001d and no source line; rerun it under a debugger with `--gtest_catch_exceptions=0` to find the line.

The Claude Code hook in `.claude/settings.json` needs `jq` on `PATH` or in mise's global config (`mise use -g jq`): it reads no project's mise config, so the `jq` above does not serve it.

Commit messages follow [Conventional Commits](https://www.conventionalcommits.org); the `commit-msg` hook checks them with cocogitto.

## License

GPL-3.0, see `LICENSE`. Code read in Krita or ComfyUI (both GPL-3.0) may inform this code; stable-diffusion.cpp is MIT.

## Credits

Tool icons in `resources/icons/` come from [Lucide](https://lucide.dev) 1.66.0 under the ISC licence (`resources/icons/LICENSE`), with the stroke colour changed to suit the dark theme.
