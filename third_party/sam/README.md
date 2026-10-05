# SAM, the Software Automatic Mouth

GLaDOS's voice: the C64's text-to-speech program from 1982, in Sebastian
Macke's C port, https://github.com/s-macke/SAM (commit a7b36ef). The files
here are its `src/`, without `main.c`.

**Licensing:** SAM has no license. It was a commercial program by Don't Ask
Software, a company that no longer exists; the port is reverse-engineered
from it, and its author cannot license it. It is included as-is and is not
covered by this project's MIT license. See
`LICENSES/LicenseRef-SAM-Abandonware.txt` (and `REUSE.toml`).

## Changes made here

- `sam.h`: `SAM_BUFFER_BYTES`, the size of the output buffer (10 s).
- `sam.c`: allocates the buffer with that size.
- `render.c`: checks every write against it; the original wrote past the
  end of the buffer for anything longer than 10 s.

Marked `tanmatsu-portal` in the code. SAM still allocates a new buffer on
every call: `main/speech.c` frees the last one first.

The files share globals the way the original's 6502 code did (`A`, `X`,
`Y`, ...), so they need `-fcommon`; CMakeLists.txt and the Makefile pass it.
