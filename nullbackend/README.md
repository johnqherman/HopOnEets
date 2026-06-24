# null FNA3D backend

Makes the headless re-sim verifier truly GPU-less. See [../docs/headless-resim.md](../docs/headless-resim.md).

## Why

The authoritative re-sim ([../src/resim.h](../src/resim.h)) only needs the deterministic **sim** to
run — never a rendered frame. The xvfb path works but still needs a real GL stack under a virtual
framebuffer. This library removes that: it shadows the exact **29 `FNA3D_*` entry points** Eets
imports (enumerated from `eets_addr_win.h`) with no-ops that hand back dummy opaque handles, so the
game boots and ticks its fixed-timestep sim with no GPU and no window.

SDL's built-in `dummy` video/audio drivers cover windowing/input/audio — this only nulls the GPU.

## Build & use

```sh
make                                    # -> libnullbackend.so
../tools/resim-runner.sh <log> --null   # uses this instead of xvfb
```

`resim-runner.sh --null` sets `SDL_VIDEODRIVER=dummy` and appends this `.so` to `LD_PRELOAD`
**after** the framework loader, so the loader's `FNA3D_SwapBuffers` interpose still fires the mod's
`Update`, then chains here (`RTLD_NEXT`) and returns immediately.

## Status / caveats

Stubs follow the **public FNA3D API** (github.com/FNA-XNA/FNA3D, `FNA3D.h`). **Not yet validated
against the shipped binary** — it's the GL-less upgrade over the xvfb default (the validated path).
Risks if the game does more than call these entry points opaquely:
- derefs a field inside the dummy device / a returned handle → widen that stub to return a small
  zeroed struct instead of `malloc(1)`.
- reads texture pixels back (`FNA3D_GetTextureData2D`) for gameplay (unlikely) → it returns zeros.
- a 30th `FNA3D_*` import in a different build → add the stub (re-run the enumeration).

If `--null` crashes on boot, fall back to xvfb (drop the flag) and file what symbol/field it touched.
