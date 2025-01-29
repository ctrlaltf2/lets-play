use letsplay_libretro_sys::*;
use std::ffi;
use tracing::*;

#[allow(dead_code)] // This *is* used; just not in Rust code
#[no_mangle]
/// This recieves log messages from our C++ helper code, and pulls them out into Tracing messages.
pub extern "C" fn letsplay_retro_frontend_log(level: LogLevel, buf: *const ffi::c_char) {
	// SAFETY: The [buf] pointer comes from the address of a C++ stack variable, so if it is null we have other problems to worry about.
	// We really only should get UTF-8 errors here in the case a core spits out something invalid.
	unsafe {
		debug_assert!(!buf.is_null(), "This pointer should NEVER be null");

		match ffi::CStr::from_ptr(buf).to_str() {
			Ok(message) => match level {
				LogLevel::Debug => {
					debug!("{}", message)
				}
				LogLevel::Info => {
					info!("{}", message)
				}
				LogLevel::Warn => {
					warn!("{}", message)
				}
				LogLevel::Error => {
					error!("{}", message)
				}
			},
			Err(err) => {
				error!(
					"Core for some reason gave a broken string to log interface: {:?}",
					err
				);
			}
		}
	}
}

extern "C" {
	// We intententionally do not declare the ... varadic arm here,
	// because libretro_sys doesn't want it, and additionally,
	// that requires nightly Rust to even do, which defeats the purpose
	// of moving it into a helper.
	fn letsplay_retro_frontend_libretro_log(level: LogLevel, fmt: *const ffi::c_char);
}

/// Log interface to provide to cores.
pub(crate) static LOG_INTERFACE: LogCallback = LogCallback {
	log: letsplay_retro_frontend_libretro_log,
};
