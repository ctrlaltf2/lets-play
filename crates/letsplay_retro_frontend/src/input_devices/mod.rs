//! Input devices
pub mod retropad;
pub use retropad::*;

pub mod mouse;
pub use mouse::*;

pub mod analog_retropad;
pub use analog_retropad::*;

/// Trait/abstraction for implementing Libretro input devices.
pub trait InputDevice {
	/// Gets the device type. This should never EVER change, and simply return a constant.
	fn device_type(&self) -> u32;

	/// Returns true if the input device is compatible with
	/// the given libretro device ID.
	///
	/// This is needed because Libretro will often look up a device with
	/// either its "base" type (i.e: RetroPad) and then ask for a subclass
	/// by changing the ID to what it wants to look for (say, Analog RetroPad).
	///
	/// Therefore, a simple "id matches exactly" comparision doesn't work.
	fn device_type_compatible(&self, id: u32) -> bool;

	/// Gets the state of one button/axis.
	/// is_pressed(index, id) can simply be expressed in a digital way as `(get_index(index, id) != 0)`.
	/// analog could be `(get_index(index_id) as f32 / 32768.)`
	fn get_index(&self, _index: u32, _id: u32) -> i16;

	/// Like get_index, but prescales to a float in the inclusive range [-1.0 .. 1.0].
	/// (This is how I want all the API's to be at some point)
	fn get_axis(&self, index: u32, id: u32) -> f32 {
		self.get_index(index, id) as f32 / 32768.
	}

	/// Clears the state of all buttons/axes.
	fn reset(&mut self);

	/// Presses a button. [pressure] is permitted to be ignored.
	fn press_button(&mut self, id: u32, pressure: Option<i16>);

	/// Presses a joystick axis.
	/// FIXME: It may make more sense to pass a f32 here and do the scale internally.
	/// should do that for buttons too. Also do we really have to use _sys constants?
	fn press_analog_axis(&mut self, _index: u32, _id: u32, _pressure: Option<i16>) {}
}
