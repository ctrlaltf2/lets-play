//! RetroPad
use super::InputDevice;
use crate::libretro_sys_new;

/// Implementation of the [InputDevice] trait for the Libretro
/// RetroPad; which is essentially a standard PS1 controller,
/// with a couple more buttons inherited from the Dual Analog/DualShock.
pub struct RetroPad {
	buttons: [i16; 16],
}

impl RetroPad {
	pub fn new() -> Self {
		Self { buttons: [0; 16] }
	}
}

impl InputDevice for RetroPad {
	fn device_type(&self) -> u32 {
		libretro_sys_new::DEVICE_JOYPAD
	}

	fn get_button(&self, id: u32) -> i16 {
		if id > 16 {
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
		if id > 16 {
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
