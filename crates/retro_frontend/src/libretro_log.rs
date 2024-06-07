use crate::libretro_sys_new::*;
use libc;
use std::ffi;
use tracing::*;

extern "C" {
	fn vsnprintf(
		buffer: *mut libc::c_char,
		len: usize,
		fmt: *const libc::c_char,
		args: ffi::VaList,
	) -> libc::c_int;
}

unsafe extern "C" fn libretro_log(level: LogLevel, fmt: *const ffi::c_char, mut args: ...) {
	let mut buffer: [ffi::c_char; 512] = [0; 512];
	let n = vsnprintf(
		buffer.as_mut_ptr(),
		buffer.len() - 1,
		fmt,
		args.as_va_list(),
	);

	if n == -1 {
		return;
	}

	// Remove the last newline. This is messy and probably not the best code to do so,
	// but I'm not aware of any alternatives that *don't* imply making a alloc::String
	// (which I don't want to do in this case) soooo /shrug
	buffer[n as usize - 1] = 0;

	match ffi::CStr::from_ptr(buffer.as_ptr()).to_str() {
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
			error!("core for some reason gave a broken string {:?}", err);
		}
	}
}

pub static LOG_INTERFACE: LogCallback = LogCallback {
	// This is needed because libretro_sys actually defines the log printf callback
	// *without* the last varadic arm; presumably because it was a nightly feature even back then.
	// (which is very upsetting considering it still is, but meh.)
	// That's kind of very important to a printf-like function.
	log: unsafe {
		std::mem::transmute::<unsafe extern "C" fn(LogLevel, *const ffi::c_char, ...), LogPrintfFn>(
			libretro_log,
		)
	},
};
