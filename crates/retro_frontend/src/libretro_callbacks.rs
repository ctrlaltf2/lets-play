use crate::{frontend_impl::*, libretro_log, util};
use crate::libretro_sys_new::*;

use rgb565::Rgb565;

use std::ffi;

use tracing::{error, info, warn};

pub(crate) unsafe extern "C" fn environment_callback(
	environment_command: u32,
	data: *mut ffi::c_void,
) -> bool {
	match environment_command {
		ENVIRONMENT_GET_LOG_INTERFACE => {
			*(data as *mut LogCallback) = libretro_log::LOG_INTERFACE.clone();
			return true;
		},

		ENVIRONMENT_SET_PERFORMANCE_LEVEL => {
			let level = *(data as *const ffi::c_uint);
			info!("Core is performance level {level}");
			return true;
		}

		ENVIRONMENT_GET_CAN_DUPE => {
			*(data as *mut bool) = true;
			return true;
		}

		ENVIRONMENT_GET_SYSTEM_DIRECTORY => {
			*(data as *mut *const ffi::c_char) = FRONTEND_IMPL.system_directory.as_ptr();
			return true;
		}

		ENVIRONMENT_GET_SAVE_DIRECTORY => {
			*(data as *mut *const ffi::c_char) = FRONTEND_IMPL.save_directory.as_ptr();
			return true;
		}

		ENVIRONMENT_SET_PIXEL_FORMAT => {
			let _pixel_format = *(data as *const ffi::c_uint);
			let pixel_format = PixelFormat::from_uint(_pixel_format).unwrap();
			FRONTEND_IMPL.pixel_format = pixel_format;
			return true;
		}

		ENVIRONMENT_SET_GEOMETRY => {
			if data.is_null() {
				return false;
			}

			let geometry = (data as *const GameGeometry).as_ref().unwrap();

			FRONTEND_IMPL.fb_width = geometry.base_width;
			FRONTEND_IMPL.fb_height = geometry.base_height;

			if let Some(resize_callback) = &mut FRONTEND_IMPL.video_resize_callback {
				resize_callback(geometry.base_width, geometry.base_height);
			}
			return true;
		}

		ENVIRONMENT_GET_VARIABLE => {
			// Make sure the core actually is giving us a pointer to a *Variable
			// so we can (if we have it!) fill it in.
			if data.is_null() {
				return false;
			}

			let var = (data as *mut Variable).as_mut().unwrap();

			match ffi::CStr::from_ptr(var.key).to_str() {
				Ok(_key) => {
					//info!("Core wants to get variable \"{}\" from us", key);
					return false;
				}
				Err(err) => {
					error!("Core gave an invalid key: {:?}", err);
					return false;
				}
			}
		}

		ENVIRONMENT_GET_VARIABLE_UPDATE => {
			// We currently pressent no changed variables to the core.
			*(data as *mut bool) = false;
			return true;
		}

		_ => {
			warn!("Environment callback called with currently unhandled command: {environment_command}");
			return false
		}
	}
}

pub(crate) unsafe extern "C" fn video_refresh_callback(
	pixels: *const ffi::c_void,
	width: ffi::c_uint,
	height: ffi::c_uint,
	pitch: usize,
) {
	// I guess this must be how duplicated frames are signaled.
	// one word: Bleh
	if pixels.is_null() {
		return;
	}

	//info!("Video refresh called, {width}, {height}, {pitch}");

	// bleh
	FRONTEND_IMPL.fb_width = width;
	FRONTEND_IMPL.fb_height = height;
	FRONTEND_IMPL.fb_pitch =
		pitch as u32 / util::bytes_per_pixel_from_libretro(FRONTEND_IMPL.pixel_format);

	let pitch = FRONTEND_IMPL.fb_pitch as usize;

	match FRONTEND_IMPL.pixel_format {
		PixelFormat::RGB565 => {
			let pixel_data_slice = std::slice::from_raw_parts(
				pixels as *const u16,
				(pitch * height as usize) as usize,
			);

			// Resize the pixel buffer if we need to
			if (pitch * height as usize) as usize != FRONTEND_IMPL.converted_pixel_buffer.len() {
				info!("Resizing RGB565 -> RGBA buffer");
				FRONTEND_IMPL
					.converted_pixel_buffer
					.resize((pitch * height as usize) as usize, 0);
			}

			// TODO: Make this convert from weird pitches to native resolution where possible.
			for x in 0..pitch as usize {
				for y in 0..height as usize {
					let rgb = Rgb565::from_rgb565(pixel_data_slice[y * pitch as usize + x]);
					let comp = rgb.to_rgb888_components();

					// Finally save the pixel data in the result array as an XRGB8888 value
					FRONTEND_IMPL.converted_pixel_buffer[y * pitch as usize + x] =
						((comp[0] as u32) << 16) | ((comp[1] as u32) << 8) | (comp[2] as u32);
				}
			}

			if let Some(update_callback) = &mut FRONTEND_IMPL.video_update_callback {
				update_callback(&FRONTEND_IMPL.converted_pixel_buffer.as_slice());
			}
		}
		_ => {
			let pixel_data_slice = std::slice::from_raw_parts(
				pixels as *const u32,
				(pitch * height as usize) as usize,
			);

			if let Some(update_callback) = &mut FRONTEND_IMPL.video_update_callback {
				update_callback(&pixel_data_slice);
			}
		}
	}
}

pub(crate) unsafe extern "C" fn input_poll_callback() {
	// TODO tell consumer about this.
	//info!("Input poll called");
}

pub(crate) unsafe extern "C" fn input_state_callback(
	_port: ffi::c_uint,
	_device: ffi::c_uint,
	_index: ffi::c_uint,
	_button_id: ffi::c_uint,
) -> ffi::c_short {
	// For now, consumer should have a say in this though
	//info!("input state port {port} device {device}, index {index}, id {button_id}");
	0
}

//pub(crate) unsafe extern "C" fn libretro_audio_sample_callback(left: ffi::c_short, right: ffi::c_short) {
//info!("audio sample called");
//}

pub(crate) unsafe extern "C" fn audio_sample_batch_callback(
	// Is actually a [[l, r]] interleaved pair.
	_samples: *const i16,
	frames: usize,
) -> usize {
	//info!("Audio batch called");
	frames
}
