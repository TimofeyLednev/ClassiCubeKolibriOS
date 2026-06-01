# ClassiCube on Plan 9 (9front)

A native [Plan 9 / 9front](http://9front.org) port of ClassiCube, built with the
native `kencc` toolchain (`6c`/`8c` + `mk`) and `libdraw` — no POSIX/APE layer.

## Status

Initial port. What works:

- **Window + rendering** via `libdraw` (`initdraw`, `allocimage`/`loadimage`/`draw`/`flushimage`),
  rendering the software framebuffer into a `rio` window.
- **Graphics backend**: `SOFTMIN` (the simplest pure-software rasterizer — no GPU/FPU assumptions).
- **Input**: keyboard via `/dev/kbd` (press/release derived by diffing the pressed-rune set each
  frame, so held-key movement works) and mouse via the `event(2)` interface.
- **Filesystem / time** mapped onto native Plan 9 system calls (`open`/`pread`/`seek`/`dirread`, `time`/`nsec`).
- **Clipboard** via `/dev/snarf`.

Not yet implemented (intentionally, for this first cut):

- Networking / multiplayer (`CC_BUILD_NETWORKING` is off — single-player only for now).
- Audio (`Null` backend).
- `SOFTGPU` / `SOFTFP` graphics backends and FPU fast paths (planned follow-ups).

## Building

You need a 9front system (or any Plan 9 with the standard `libdraw`/`libc`).
From the repository root:

```
mk -f misc/plan9/mkfile
```

This produces a `ClassiCube` binary for your `$objtype` (e.g. `amd64`). Run it with:

```
./ClassiCube
```

`mk -f misc/plan9/mkfile install` copies it into `$home/bin/$objtype`.
`mk -f misc/plan9/mkfile clean` removes build objects.

## Layout

- `src/plan9/Platform_Plan9.c` — platform layer: files, directories, time, process, logging.
- `src/plan9/Window_Plan9.c`   — window, framebuffer blit, keyboard/mouse input, clipboard.
- `misc/plan9/mkfile`          — the `mk` build file (this directory).

The platform is selected by the `-DPLAT_PLAN9` flag in the mkfile, which activates the
`CC_BUILD_PLAN9` block in `src/Core.h`.

## Notes

- The keyboard layer reads `/dev/kbd`, which can only be opened by the host owner — run from a
  normal `rio` window as yourself.
- The framebuffer is repacked from ClassiCube's `RGBA` byte order to Plan 9's `RGB24` (`B,G,R`)
  on each frame before `loadimage`.
