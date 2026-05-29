# trender
A CPU-only software 3D renderer that outputs directly to the terminal via Sixel graphics. Loads a textured OBJ mesh and renders it frame after frame with interactive camera controls — no GPU, no windowing system required.

## Pipeline (per frame)

**1. Load mesh + textures**
- Loads an OBJ file via `tinyobj_loader_c`
- Loads all diffuse textures via `stb_image` and packs them into a single flat **texture atlas** in RGB565 (16-bit) format
- This avoids any per-triangle texture switching during rasterization

**2. Vertex transform**
- Each vertex is multiplied by the model-view-projection matrix to get homogeneous clip-space coordinates

**3. Near-plane clipping**
- Triangles straddling the near plane are clipped: 1 inside vertex → 1 triangle, 2 inside vertices → 2 triangles

**4. Rasterization — index buffer only**
- Uses a half-edge fixed-point algorithm, processing **8 pixels at a time with AVX2**
- For each covered pixel: depth test, perspective-correct UV interpolation, compute atlas texel index
- Writes the atlas texel index (not color yet) into a 32-bit index buffer, and depth into a float depth buffer

**5. Texture sampling pass**
- Walks the index buffer and looks up the atlas to fill the RGB565 output framebuffer

**6. Sixel conversion**
- Quantizes the RGB565 output to a **216-color 6×6×6 RGB cube palette** with **Bayer ordered dithering** (16×16 matrix)
- AVX2-accelerated, processes 16–32 pixels at a time

**7. Sixel encoding**
- Encodes the palette-indexed bitmap into the Sixel terminal graphics protocol with run-length compression
- Writes the escape sequence directly to stdout

## Architecture

- **OpenMP** parallelism: thread 0 handles input events and display output; threads 1+ run the render pipeline on horizontal strips of the image
- **Triple-buffering** with OMP locks between render threads and the display thread to avoid tearing
- **Input** via raw terminal I/O: `wasd`/`qe` for camera movement, arrow keys to look, left-click raycasts to isolate a single triangle for debugging

## Key Design Decisions

| Decision | Reason |
|---|---|
| RGB565 internal format | Halves memory bandwidth vs RGBA32 |
| Single texture atlas | No branching or state changes mid-rasterize |
| Index buffer → separate texture pass | Decouples rasterization from sampling; lets the depth test run without touching texture memory |
| AVX2 throughout | 8–32× throughput on the inner loops |
| Sixel output | Works in any Sixel-capable terminal (iTerm2, kitty, etc.) |

## Todo

- [x] Refactor input handling
- [x] Fix ctrl+mouse move issue
- [x] Add Ctrl+Fn keys support
- [x] Add Windows support
- [x] Refactor output handling
- [ ] Add Alt+any keys support
- [ ] Refactor `main.c`
- [ ] Split sixel encoding and conversion to index bitmap
- [ ] Experiment with different mouse reporting modes
