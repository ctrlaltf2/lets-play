use crate::libretro_sys_new::*;
use std::ffi;
use tracing::*;

#[allow(dead_code)] // This *is* used; just not in Rust code
#[no_mangle]
/// This recieves log messages from our C++ helper code, and pulls them out into Tracing messages.
pub extern "C" fn libretro_log_recieve(level: LogLevel, buf: *const ffi::c_char) {
	let mut msg: Option<&str> = None;

	// Safety: This pointer is never null, and always comes from the stack;
	// we really only should get UTF-8 errors here in the case a core spits out something invalid.
	unsafe {
		match ffi::CStr::from_ptr(buf).to_str() {
			Ok(message) => msg = Some(message),
			Err(err) => {
				error!(
					"Core for some reason gave a broken string to log interface: {:?}",
					err
				);
			}
		}
	}

	if let Some(message) = &msg {
		match level {
			LogLevel::Debug => {
				debug!("Core log: {}", message)
			}
			LogLevel::Info => {
				info!("Core log: {}", message)
			}
			LogLevel::Warn => {
				warn!("Core log: {}", message)
			}
			LogLevel::Error => {
				error!("Core log: {}", message)
			}
		}
	}
}

extern "C" {
	// We intententionally do not declare the ... varadic arm here,
	// because libretro_sys doesn't want it, and additionally,
	// that requires nightly Rust to even do, which defeats the purpose
	// of moving it into a helper.
	fn libretro_log(level: LogLevel, fmt: *const ffi::c_char);
}

pub static LOG_INTERFACE: LogCallback = LogCallback { log: libretro_log };
