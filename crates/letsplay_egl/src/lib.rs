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

	/// A helper to get a display on the EGL "Device" platform, which allows headless rendering,
	/// without any window system interface.
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

	// link EGL as a library dependency
	#[link(name = "EGL")]
	extern "C" {}
}

pub use egl_impl::*;
