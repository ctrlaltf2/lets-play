use gl_generator::{Api, Fallbacks, Profile, Registry, StaticGenerator};
use std::env;
use std::fs::File;
use std::path::Path;

fn main() {
	// EGL

	let dest = env::var("OUT_DIR").unwrap();
	let mut file = File::create(&Path::new(&dest).join("egl_bindings.rs")).unwrap();

	Registry::new(
		Api::Egl,
		(1, 5),
		Profile::Core,
		Fallbacks::All,
		[
			"EGL_EXT_platform_base",
			"EGL_EXT_device_base",
			// This allows getting OpenGL APIs via eglGetProcAddress()
			"EGL_KHR_get_all_proc_addresses",
			"EGL_KHR_client_get_all_proc_addresses",
			"EGL_EXT_platform_device",
		],
	)
	.write_bindings(StaticGenerator, &mut file)
	.unwrap();
}
