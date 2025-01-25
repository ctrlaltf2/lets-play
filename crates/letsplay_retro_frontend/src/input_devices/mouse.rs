//! Mouse
use super::InputDevice;
use crate::libretro_sys_new;

/// Implementation of the [InputDevice] trait for the Libretro mouse.
pub struct Mouse {
	buttons: [i16; 8],
	// TODO: hold the last x/y so we can calculate relative position
}

impl Mouse {
	pub fn new() -> Self {
		Self { buttons: [0; 8] }
	}

	// helpers:
	//
	// set_pos(x, y) (will do all the stupid scaling, and press the axes with the right value)
	// set_buttons(mask) (will press all buttons, including wheel based on standard button mask)
}

impl InputDevice for Mouse {
	fn device_type(&self) -> u32 {
		libretro_sys_new::DEVICE_MOUSE
	}

	fn device_type_compatible(&self, id: u32) -> bool {
		id == self.device_type()
	}

	fn get_index(&self, _index: u32, id: u32) -> i16 {
		if id > 8 {
			return 0;
		}

		self.buttons[id as usize]
	}

	fn reset(&mut self) {
		for button in &mut self.buttons {
			*button = 0i16;
		}
	}

	fn press_button(&mut self, id: u32, pressure: Option<i16>) {
		if id > 8 {
			return;
		}

		match pressure {
			Some(pressure_value) => {
				self.buttons[id as usize] = pressure_value;
			}
			None => {
				// ? or 0x7fff ? Unsure
				self.buttons[id as usize] = 0x7fff;
			}
		}
	}
}
