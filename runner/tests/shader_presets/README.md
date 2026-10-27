Build this ROM-free presenter against SDL3, then run `SDL_VIDEODRIVER=offscreen python3 check.py /path/to/presenter` (or use your normal SDL video driver).

The check renders a synthetic pattern through all four real GLSL presets, verifies nonblack and distinct framebuffer captures, and requires a missing preset to match the unfiltered capture. Temporary screenshots are removed automatically. This requires a working OpenGL 3.3 context; it is not a dummy-driver test.
