# nfs-recompiled-vita

A **PlayStation Vita** port of [nfs-recompiled](https://github.com/AlirezaXdehghan/nfs-recompiled)
— a static recompilation of **Need for Speed II: Special Edition** and **Need
for Speed III: Hot Pursuit** from their original Win32 x86 executables into
portable C++. This fork adds the Vita target: it builds with **vitasdk**,
renders through **vitaGL**, runs on **SDL3**.

---

## Building

You need a Linux/macOS box (or WSL2) with **[vitasdk](https://vitasdk.org)**,
plus **vitaGL** and **SDL3** built for `arm-vita-eabi`. Full one-time setup is in
[vita/README_VITA.md §2](vita/README_VITA.md#2-toolchain-setup-one-time).

```bash
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH

cmake -S vita -B build-vita -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.cmake
cmake --build build-vita -j$(nproc)
# -> build-vita/nfs3hp.vpk
# -> build-vita/nfs2se.vpk
```

CMake fetches `sse2neon.h` automatically (needs network at configure time).

| CMake option | Default | Effect |
|---|---|---|
| `CODE_SIZE_OPT` | `OFF` | Build the (large) recompiled image with `-Os` instead of `-O2` if it won't fit the memory budget. |

---

## Installing on the Vita

The recompilation ships **no game content**, you must supply the original data.

1. Install the game from CD (e.g. via Wine; see the upstream project), and copy the installation to ux0:/data/nfs3hp for nfs3 and ux0:/data/nfs2se for nfs2
2. Install the corresponding `.vpk` with VitaShell.
```
ux0:data/nfs3hp/      # Need for Speed III: Hot Pursuit
├── Fedata/
├── GameData/
└── ...               # whatever install.win references
ux0:data/nfs2se/      # Need for Speed II: SE (same layout)
```

vitaGL additionally requires `libshacccg.suprx`
extracted on the console for runtime shader compilation.

---

## Controls

The pad is opened at boot and drives the games through their existing keyboard
input path. Mapping uses SDL3's real Vita
button indices:

| Control | Action |
|---|---|
| Left stick | Steer ←→ · accelerate/brake ↑↓ · menu navigation |
| ✕ Cross / Start | Confirm / select (Enter) |
| ○ Circle / Select | Back / cancel (Escape) |
| D-pad | Menu navigation (arrows) |
| △ Triangle, □ Square, L, R | Space |

Right-stick aiming and analog throttle are reported but not yet mapped.

### Controls on Vita (NFS II SE)

NFS2 gets the pad as keyboard input only (the DirectInput joystick is hidden so
the game no longer sees every press twice). NFS3 is unchanged.

| Vita | Action |
| --- | --- |
| D-pad / left stick | Steer, accelerate (up), brake (down), menu navigation |
| R / L | Accelerate / brake |
| Cross | Confirm (Enter) |
| Circle / Start | Back / pause (Esc) |
| Square | Handbrake (Space) |
| Triangle | Change camera (C) |
| Select | Horn (H) |
| Right stick left / right | Downshift (Z) / upshift (A) |
| Right stick down | Look behind (hold B) |

Glide `grClipWindow` is now honoured as a GL scissor, and alpha test / chromakey
state is applied per draw call instead of once per frame.

---

## Credits

- **nfs-recompiled** — the upstream static recompilation this port is built on.
- **[vitaGL](https://github.com/Rinnegatamante/vitaGL)** — OpenGL implementation
  for the Vita.
- **[vitasdk](https://vitasdk.org)** and **SDL3** — toolchain and platform layer.
- Port approach follows the footsteps of **re3-vita**.

Need for Speed is a trademark of Electronic Arts. This project ships no game
assets and is for use with legally-obtained original game data.
