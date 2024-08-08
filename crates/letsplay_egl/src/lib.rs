//! EGL bindings and helpers.

#[allow(non_camel_case_types)]
#[allow(unused_imports)]
mod egl_impl {
	pub type khronos_utime_nanoseconds_t = khronos_uint64_t;
	pub type khronos_uint64_t = u64;
	pub type khronos_ssize_t = std::ffi::c_long;
	pub type EGLint = i32;
	pub type EGLNativeDisplayType = *const std::ffi::c_void;
	pub type EGLNativePixmapType = *const std::ffi::c_void;
	pub type EGLNativeWindowType = *const std::ffi::c_void;

	pub type NativeDisplayType = EGLNativeDisplayType;
	pub type NativePixmapType = EGLNativePixmapType;
	pub type NativeWindowType = EGLNativeWindowType;

	include!(concat!(env!("OUT_DIR"), "/egl_bindings.rs"));

	// link EGL as a library dependency
	#[link(name = "EGL")]
	extern "C" {}
}

/// Helper code for making EGL easier to use.
pub mod helpers {
	use super::egl_impl as egl;
	use egl::*;

	// TODO: Move these helpers to a new "helpers" module.

	pub type GetPlatformDisplayExt = unsafe extern "C" fn(
		platform: types::EGLenum,
		native_display: *const std::ffi::c_void,
		attrib_list: *const types::EGLint,
	) -> types::EGLDisplay;

	pub type QueryDevicesExt = unsafe extern "C" fn(
		max_devices: self::types::EGLint,
		devices: *mut self::types::EGLDeviceEXT,
		devices_present: *mut EGLint,
	) -> types::EGLBoolean;

	/// Queries all available extensions on a display.
	pub fn get_extensions(display: types::EGLDisplay) -> Vec<String> {
		// SAFETY: eglQueryString() should never return a null pointer.
		// If it does your video drivers are more than likely broken beyond repair.
		unsafe {
			let extensions_ptr = QueryString(display, EXTENSIONS as i32);
			assert!(!extensions_ptr.is_null());

			let extensions_str = std::ffi::CStr::from_ptr(extensions_ptr)
				.to_str()
				.expect("Invalid EGL_EXTENSIONS");

			extensions_str
				.split(' ')
				.map(|str| str.to_string())
				.collect()
		}
	}

	/// A helper to get a display on the EGL "Device" platform,
	/// which allows headless rendering without any window system interface.
	pub fn get_device_platform_display(index: usize) -> types::EGLDisplay {
		const NR_DEVICES_MAX: usize = 16;
		let mut devices: [types::EGLDeviceEXT; NR_DEVICES_MAX] = [std::ptr::null(); NR_DEVICES_MAX];

		// This is how many devices are actually present,
		let mut devices_present: EGLint = 0;

		assert!(
			index <= NR_DEVICES_MAX,
			"More than {NR_DEVICES_MAX} devices are not supported right now"
		);

		unsafe {
			// TODO: These should probbaly be using CStr like above.
			let query_devices_ext: QueryDevicesExt =
				std::mem::transmute(GetProcAddress(b"eglQueryDevicesEXT\0".as_ptr() as *const i8));
			let get_platform_display_ext: GetPlatformDisplayExt = std::mem::transmute(
				GetProcAddress(b"eglGetPlatformDisplayEXT\0".as_ptr() as *const i8),
			);

			(query_devices_ext)(
				NR_DEVICES_MAX as i32,
				devices.as_mut_ptr(),
				std::ptr::addr_of_mut!(devices_present),
			);

			(get_platform_display_ext)(PLATFORM_DEVICE_EXT, devices[index], std::ptr::null())
		}
	}

	/// A wrapper over a EGL Device context. Provides easy initialization and
	/// cleanup functions.
	pub struct DeviceContext {
		display: types::EGLDisplay,
		context: types::EGLContext,
	}

	impl DeviceContext {
		pub fn new(index: usize) -> DeviceContext {
			let display = self::get_device_platform_display(index);

			let context = unsafe {
				const EGL_CONFIG_ATTRIBUTES: [types::EGLenum; 13] = [
					egl::SURFACE_TYPE,
					egl::PBUFFER_BIT,
					egl::BLUE_SIZE,
					8,
					egl::RED_SIZE,
					8,
					egl::BLUE_SIZE,
					8,
					egl::DEPTH_SIZE,
					8,
					egl::RENDERABLE_TYPE,
					egl::OPENGL_BIT,
					egl::NONE,
				];
				let mut egl_major: egl::EGLint = 0;
				let mut egl_minor: egl::EGLint = 0;

				let mut egl_config_count: egl::EGLint = 0;

				let mut config: egl::types::EGLConfig = std::ptr::null();

				egl::Initialize(
					display,
					std::ptr::addr_of_mut!(egl_major),
					std::ptr::addr_of_mut!(egl_minor),
				);

				egl::ChooseConfig(
					display,
					EGL_CONFIG_ATTRIBUTES.as_ptr() as *const egl::EGLint,
					std::ptr::addr_of_mut!(config),
					1,
					std::ptr::addr_of_mut!(egl_config_count),
				);

				egl::BindAPI(egl::OPENGL_API);

				let context =
					egl::CreateContext(display, config, egl::NO_CONTEXT, std::ptr::null());

				// Make the context current on the display so OpenGL routines "just work"
				egl::MakeCurrent(display, egl::NO_SURFACE, egl::NO_SURFACE, context);

				context
			};

			Self { display, context }
		}

		pub fn get_display(&self) -> types::EGLDisplay {
			self.display
		}

		pub fn destroy(&mut self) {
			if self.display.is_null() && self.context.is_null() {
				return;
			}

			// Release the EGL context we created before destroying it
			unsafe {
				egl::MakeCurrent(
					self.display,
					egl::NO_SURFACE,
					egl::NO_SURFACE,
					egl::NO_CONTEXT,
				);
				egl::DestroyContext(self.display, self.context);
				egl::Terminate(self.display);

				self.display = std::ptr::null();
				self.context = std::ptr::null();
			}
		}
	}

	// TODO: impl Drop? 
	// This could be problematic because OpenGL resources need to be destroyed
	// somehow *before* we are. This could be solved in a number of ways but
	// honestly I think the best one (that I can think of)
	// is to provide an explicit drop point where OpenGL resources are destroyed
	// before the EGL device context is (and then tie that to an `impl Drop for T`.).
}

pub use egl_impl::*;
