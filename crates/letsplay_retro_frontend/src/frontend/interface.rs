use std::ffi;

/// Initalization data for HW OpenGL cores.
pub struct HwGlInitData {
	/// A pointer to a function that can be used to request OpenGL extension functions.
	/// Given to the core so they can do so.
	pub get_proc_address: *mut ffi::c_void,
}

/// Interface for the frontend to call to user code.
pub trait FrontendInterface {
	/// Called when video is updated.
	fn video_update(&mut self, slice: &[u32], pitch: u32);

	/// Called when video is updated and the core is using HW OpenGL rendering.
	fn video_update_gl(&mut self);

	/// Called when resize occurs.
	fn video_resize(&mut self, width: u32, height: u32);

	// TODO(lily): This should probably return the amount of consumed frames,
	// as in some cases that *might* differ?
	fn audio_sample(&mut self, slice: &[i16], size: usize);

	/// Called to poll input.
	fn input_poll(&mut self);

	/// Initalize hardware accelerated rendering using OpenGL.
	/// Return [Option::None] to indicate OpenGL initalization has failed.
	fn hw_gl_init(&mut self) -> Option<HwGlInitData>;
}
