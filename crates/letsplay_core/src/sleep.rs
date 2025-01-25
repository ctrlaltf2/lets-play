//! Needed because std::thread::sleep_until is currently a nightly API.
use std::time::*;

//#[cfg(not(target_os = "linux"))]
pub fn sleep_until(deadline: Instant) {
	use std::thread::*;
	let now = Instant::now();

    if let Some(delay) = deadline.checked_duration_since(now) {
        sleep(delay);
    }
}

/* 
#[cfg(target_os = "linux")]
pub fn sleep_until(deadline: Instant) {
	use libc::timespec;

	unsafe {
		while libc::clock_nanosleep(
			libc::CLOCK_MONOTONIC,
			libc::TIMER_ABSTIME,
			// SAFETY: Instant's implementation on Linux
			// IS a `struct timespec`, so this is "safe" enough.
			//
			// If this ever changes (not that I expect it to),
			// this Linux-specific implementation can be removed.
			std::ptr::addr_of!(deadline) as *const timespec,
			std::ptr::null_mut(),
		) == libc::EINTR
		{}
	}
}
*/
