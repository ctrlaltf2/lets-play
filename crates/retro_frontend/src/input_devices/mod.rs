//! Input devices
pub mod retropad;
pub use retropad::*;

pub mod mouse;
pub use mouse::*;

/// Trait for implementing Libretro input devices
pub trait InputDevice {
	/// Gets the device type. This should never EVER change, and simply return a constant.
	fn device_type(&self) -> u32;

	/// Gets the state of one button/axis.
	/// is_pressed(id) can simply be expressed as `(get_button(id) != 0)`.
	fn get_button(&self, id: u32) -> i16;

	/// Clears the state of all buttons/axes.
	fn reset(&mut self);

	/// Presses a button/axis.
	fn press_button(&mut self, id: u32, pressure: Option<i16>);
}
