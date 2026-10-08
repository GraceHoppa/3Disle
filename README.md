<h1 align="center">3Disle</h1>

<p align="center">
  <b>Make explorable 3D teardowns of real hardware, without writing the page by hand.</b><br />
  Created by <b>f3nt</b> and <a href="https://github.com/OlehHavrilko"><b>Oleh Havrilko</b></a>
</p>

3Disle turns 3D models of a device and its parts into a teardown you explore in the browser.
You take it apart layer by layer, open a part to see what is inside, and dive level by level
from the whole device down to a single chip. Its look and feel follow
[GPU → Atom](https://olehhavrilko.github.io/3dgpuanimation/), a hand-built descent from a
graphics card to a silicon atom.

You bring the models (from Blender, as GLB) and describe the device: which pieces are parts,
what each one is, which part opens into which deeper model, and how power, data and motion run
through it. The engine does the rest. It is headed toward a desktop app that exports the
finished teardown as a web page you can link anywhere.

To run it, see [INSTALL.md](INSTALL.md). How it works: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md);
every field a device file can use: [docs/ENGINE.md](docs/ENGINE.md).

[MIT](LICENSE)
