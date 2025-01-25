use crate::libretro_sys_new;

use super::{InputDevice, RetroPad};

// private helper type for packaging up the stick data
struct Stick {
	pub x: i16,
	pub y: i16,
}

impl Stick {
	fn new() -> Self {
		Self { x: 0, y: 0 }
	}

	fn clear(&mut self) {
		self.x = 0;
		self.y = 0;
	}
}

/// Implementation of the [InputDevice] trait for the
/// Analog RetroPad. Currently, this is mostly a stub which calls
/// into the RetroPad implementation w/out actually implementing
/// any of the analog axes or addl. features.
pub struct AnalogRetroPad {
	pad: RetroPad,
	left_stick: Stick,
	right_stick: Stick,
}

impl AnalogRetroPad {
	pub fn new() -> Self {
		Self {
			pad: RetroPad::new(),
			left_stick: Stick::new(),
			right_stick: Stick::new(),
		}
	}
}

// Sidenote: I really don't like the fact I have to manually thunk,
// but thankfully there's only like one, maybe 2 levels of subclassing
// in the Libretro input APIs, so it won't grow too awfully..

impl InputDevice for AnalogRetroPad {
	fn device_type(&self) -> u32 {
		libretro_sys_new::DEVICE_ANALOG
	}

	fn device_type_compatible(&self, id: u32) -> bool {
		if self.pad.device_type_compatible(id) {
			// If the RetroPad likes it, then so do we.
			true
		} else {
			// Check for the analog type
			id == libretro_sys_new::DEVICE_ANALOG
		}
	}

	fn reset(&mut self) {
		self.pad.reset();
		self.left_stick.clear();
		self.right_stick.clear();
	}

	fn get_index(&self, index: u32, id: u32) -> i16 {
		// Nasty but you can blame libretro.
		let fallback = self.pad.get_index(0, id);
		return match index {
			libretro_sys_new::DEVICE_INDEX_ANALOG_LEFT => match id {
				libretro_sys_new::DEVICE_ID_ANALOG_X => self.left_stick.x,
				libretro_sys_new::DEVICE_ID_ANALOG_Y => self.left_stick.y,
				_ => fallback,
			},
			libretro_sys_new::DEVICE_INDEX_ANALOG_RIGHT => match id {
				libretro_sys_new::DEVICE_ID_ANALOG_X => self.right_stick.x,
				libretro_sys_new::DEVICE_ID_ANALOG_Y => self.right_stick.y,
				_ => fallback,
			},

			_ => 0i16,
		};
	}

	fn press_button(&mut self, id: u32, pressure: Option<i16>) {
		// FIXME: "press" axes.
		self.pad.press_button_friend(id, pressure);
	}

	fn press_analog_axis(&mut self, index: u32, id: u32, pressure: Option<i16>) {
		let pressure = if let Some(pressure_value) = pressure {
			pressure_value
		} else {
			//0x4000 // 0.5 in Libretro's mapping
			0x7fff // 1.0 in Libretro's mapping
		};

		match index {
			libretro_sys_new::DEVICE_INDEX_ANALOG_LEFT => match id {
				libretro_sys_new::DEVICE_ID_ANALOG_X => self.left_stick.x = pressure,
				libretro_sys_new::DEVICE_ID_ANALOG_Y => self.left_stick.y = pressure,
				_ => {}
			},

			libretro_sys_new::DEVICE_INDEX_ANALOG_RIGHT => match id {
				libretro_sys_new::DEVICE_ID_ANALOG_X => self.right_stick.x = pressure,
				libretro_sys_new::DEVICE_ID_ANALOG_Y => self.right_stick.y = pressure,
				_ => {}
			},

			_ => {}
		}
	}
}
