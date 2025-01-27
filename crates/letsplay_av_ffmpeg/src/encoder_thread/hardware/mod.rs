//! Hardware encoding encoder thread implementations.
pub mod nvenc;

// FIXME: Provide a VA-API based "libre" implementation
// that we can always build. This should be possible:
// - We can already grab the EGL context and share it between threads
// - EGLImage allows us to grab a eais
// - There are OpenCL extensions to either create a image2d via a EGLImage or a OpenGL texture
// so we can absoultely do it; It's just going to be a pain.
