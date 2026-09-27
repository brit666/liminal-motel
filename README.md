# 🏨 Liminal Motel

> An endless, procedurally generated motel you can never fully map — walk through infinite hotel rooms rendered in real time with OpenGL.

Liminal Motel is a first-person 3D application built in C++ with OpenGL 3.3 (Core Profile). Instead of loading a fixed level, every room is generated on the fly from a deterministic spatial hash of its grid coordinates — so the world is infinite, but revisiting a room always shows you exactly what you saw before.

## ✨ Features

- **Deterministic procedural generation** — a custom 3D spatial hash (`hash3D`) decides each room's color theme, furniture layout, and door connections from its `(gridX, gridZ)` coordinates alone. No random seeds, no persistent save data — the same coordinates always regenerate the same room.
- **Bounded active grid** — only a 3×3 window of rooms around the player is ever generated at once, sliding along as you move, keeping memory and draw calls constant no matter how far you walk.
- **Room archetypes** — four furniture layouts (Study Bedroom, Lounge, Fireside Suite, Reading Nook) and five color palettes, assigned per room so the motel doesn't feel like the same room copy-pasted forever.
- **Proximity-triggered sliding doors** — pocket doors on every wall slide open as you approach and slide shut as you leave, with connections between rooms guaranteed to line up on both sides.
- **Multi-light illumination** — 4 point lights (ceiling lamps) per room plus 2 spotlights: a toggleable camera-mounted flashlight and a per-room desk lamp.
- **Dual shading pipelines** — switch at runtime between per-fragment Phong shading and per-vertex Gouraud shading to compare the two.
- **Animated ceiling fans** — continuously rotating, driven by frame time, with a light kit built in.
- **Collision & movement** — an axis-decoupled 2D bounding-box system lets you slide smoothly along walls and furniture instead of clipping through geometry or getting stuck.

## 🎮 Controls

| Input | Action |
|---|---|
| `W` `A` `S` `D` | Move (locked to the horizontal plane) |
| Mouse | Look around |
| Scroll wheel | Zoom |
| `G` | Toggle Phong / Gouraud shading |
| `F` | Toggle the camera flashlight |
| `ESC` | Quit |

## 📸 Screenshots

<img width="1282" height="992" alt="image" src="https://github.com/user-attachments/assets/e72c1614-2466-45c3-a47d-f9c0a9262ec0" />
<img width="1282" height="992" alt="image" src="https://github.com/user-attachments/assets/a693be91-e35a-4fa8-aa30-1fccf2923503" />
<img width="1282" height="992" alt="image" src="https://github.com/user-attachments/assets/3f14303f-4ce9-4729-b955-5354d110d8b4" />

##Phong(1)-vs-Gouraud(2)
<img width="1282" height="992" alt="image" src="https://github.com/user-attachments/assets/1c4cc66d-d259-431e-a178-c7a16ef21005" />
<img width="1282" height="992" alt="image" src="https://github.com/user-attachments/assets/d84d1aad-ebcf-4772-adce-b90c74113570" />



## 🎥 Demo Video

https://github.com/user-attachments/assets/3439d175-0d2e-4ffd-9f5e-eb20594dd0dc


## 📥 Download

Prebuilt Windows binaries are available on the **[Releases](../../releases)** page — no need to install GLFW, GLAD, or GLM yourself. Just download the zip, extract it, and run the `.exe`.

> Requires the [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe) — most Windows machines already have this installed.

## 🛠️ Tools & Technologies

- **C++17**
- **OpenGL 3.3** (Core Profile)
- **GLFW3** — windowing, input, and timing
- **GLAD** — OpenGL function loader
- **GLM** — vector/matrix math
- **GLSL 330 Core** — Phong and Gouraud vertex/fragment shaders

## 🚀 Building from Source

This repo includes a Visual Studio solution (`Lighting.sln`), targeting **Visual Studio 2022 (v143 toolset)** on Windows.

1. Clone the repo:
git clone https://github.com/brit666/liminal-motel.git


2. Download **GLFW** (precompiled binaries for your VS version) and **GLM** (header-only), and place them somewhere on disk — e.g. `C:\opengl\Include` and `C:\opengl\Lib`.
3. Open `Lighting.sln` in Visual Studio, select the **x64** platform, and open **Project → Properties**:
   - Under **VC++ Directories**, set **Include Directories** to your GLFW + GLM include path, and **Library Directories** to your GLFW lib path.
   - Under **Linker → Input → Additional Dependencies**, make sure `glfw3.lib;opengl32.lib` are listed.
   - *(Only the Debug|x64 configuration has these already set in the committed project file, pointing at a local machine path — you'll need to repeat this for Release|x64, or just re-point Debug|x64 to your own GLFW/GLM location.)*
4. Confirm `glad.c` in **Solution Explorer → Source Files** resolves correctly — the committed project references it via a relative path from the original author's machine. If Visual Studio shows it as missing, right-click it → **Remove**, then **Add → Existing Item** and point it at the `glad.c` in this repo's root instead.
5. Build and run. The shader files (`.vs`, `.fs`, `.glsl`) are loaded from disk at runtime, so keep them in the same folder as the built `.exe` (Visual Studio's default output folder already sits alongside the source, so this works out of the box).

## 🧠 How It Works

**Procedural generation.** Every room's identity comes from hashing its integer grid coordinates through `hash3D`, a bit-mixing function seeded with prime multipliers. That single deterministic value decides the room's color theme, its furniture archetype, which walls have doors, and where furniture sits — so nothing needs to be stored between visits.

**The 3×3 active grid.** `ProceduralGenerator::GenerateRoomGrid` keeps only the 9 rooms immediately around the camera in memory at any time. When the player crosses into a new grid cell, the window re-centers: far rooms are dropped, new ones are generated from their hash. This is what makes the world feel infinite without ever holding more than a handful of rooms in memory.

**Doors.** Door presence is decided per shared *edge* between two rooms (not per room), so neighboring rooms always agree on whether a doorway connects them. Each door tracks the player's distance every frame and slides open/closed accordingly.

**Lighting.** Point lights simulate ceiling lamps with distance-based attenuation; spotlights (the flashlight and desk lamp) restrict light to a cone with smooth inner/outer falloff. Both Phong (per-fragment) and Gouraud (per-vertex) shading are implemented so you can compare the trade-offs live.

**Collision.** Every wall segment, door leaf, and furniture piece contributes an axis-aligned box on the XZ plane. Movement is resolved independently on the X and Z axes each frame, which is what lets you slide along a wall instead of stopping dead or clipping through it.

## 📁 Project Structure
