use super::{AnalogRetroPad, InputDevice, RetroPad};

/// A helper "polymorphic" input device.
/// This does not implement InputDevice directly,
/// rather it exposes a single device type which you
/// can permissively lease.
pub enum AnyDevice {
	Pad(RetroPad),
	AnalogPad(AnalogRetroPad),
}

impl AnyDevice {
	/// Creates a new AnyDevice with a RetroPad in it.
	pub fn new_pad() -> Self {
		Self::Pad(RetroPad::new())
	}

	/// Creates a new AnyDevice with a AnalogRetroPad in it.
	pub fn new_analog_pad() -> Self {
		Self::AnalogPad(AnalogRetroPad::new())
	}

	/// Gets the [RetroPad]. Returns [Option::None] if this AnyDevice
	/// does not have a RetroPad.
	pub fn get_pad(&self) -> Option<&RetroPad> {
		match self {
			Self::Pad(p) => Some(&p),
			_ => None,
		}
	}

	/// Gets the [AnalogRetroPad]. Returns [Option::None] if this AnyDevice
	/// does not have a RetroPad.
	pub fn get_analog_pad(&self) -> Option<&AnalogRetroPad> {
		match self {
			Self::AnalogPad(p) => Some(&p),
			_ => None,
		}
	}

	/// Mutable version of [AnyDevice::get_pad].
	pub fn get_pad_mut(&mut self) -> Option<&mut RetroPad> {
		match self {
			Self::Pad(p) => Some(p),
			_ => None,
		}
	}

	/// Mutable version of [AnyDevice::get_analog_pad].
	pub fn get_analog_pad_mut(&mut self) -> Option<&mut AnalogRetroPad> {
		match self {
			Self::AnalogPad(p) => Some(p),
			_ => None,
		}
	}

	/// Gets whatever is in here ready for attachment to a frontend.
	/// (A *mut dyn InputDevice trait object)
	/// 
	/// # Safety 
	/// The frontend must outlive this AnyDevice, or this AnyDevice must
	/// be unplugged before it is dropped.
	pub fn get_dyn_mut(&mut self) -> *mut dyn InputDevice {
		match self {
			Self::Pad(p) => p as *mut dyn InputDevice,
			Self::AnalogPad(p) => p as *mut dyn InputDevice,
		}
	}
}
