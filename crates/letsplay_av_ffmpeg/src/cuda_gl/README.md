# `cuda_gl`

This module is provided because `cudarc` does not include or use `cudaGL.h`. 

CUDA provides graphics interopability (which allows CUDA to alter PBO, textures, so on.). 

While `cudarc` generates sys bindings for the generic subset of APIs defined in the CUDA Driver API, we need the OpenGL-specific APIs, which it does *not* generate.

As far as I know the subset of API's this provides go down from CUDA 11.5 - 12.1. So it should be relatively stable and not need a re-bindgen for any new cuda version. One can be perodically done anyways using the `./bindgen.sh` script if desired.

## Why would we need that?

CUDA's OpenGL interop allows us to get a CUDA array handle to a OpenGL texture. This is cool because we can directly copy it using cuMemcpy2D() to a allocated buffer (e.g: a ffmpeg HW frame). 

The usefulness of this is rather limited thanks to the OpenGL coordinate system being the way it is, however, we can expand the idea a bit (wasting some GPU memory in the process, but /shrug), and encode frames directly from libretro without leaving the GPU (on NVENC, we don't even need to launch a format conversion kernel!). If that sounds pretty cool, that's because it is. 

Under ~2ms average encode latency even with all the fun stuff needed to deal with OpenGL being OpenGL!.

