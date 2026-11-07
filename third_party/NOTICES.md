# Third-party notices

| What | Where | License |
| --- | --- | --- |
| stb_truetype, stb_image_write, stb_image (Sean Barrett) | `third_party/` | MIT or public domain |
| OpenGL ES 3 constants and prototypes (Khronos), the ones the engine uses | `wasm/include/GLES3/gl3.h` | MIT |
| JetBrains Mono, subset | `assets/` | SIL OFL 1.1, `third_party/JetBrainsMono-OFL.txt` |
| three.js: shading model, room environment values, extrude and curve algorithms, ported to C and GLSL | `engine/mesh.c`, `gfx/` | MIT |
| postprocessing (Raoul van Rüschen): bloom, grade and tone-map passes, ported to GLSL | `gfx/shaders/` | Zlib |
| earcut (Mapbox): triangulation, ported to C | `engine/mesh.c` | ISC |
| GPU → Atom (Oleh Havrilko): material, light and grade values | `engine/model.c`, `gfx/render.c`, `legacy-ts/src/lib/` | MIT, `LICENSE` |
