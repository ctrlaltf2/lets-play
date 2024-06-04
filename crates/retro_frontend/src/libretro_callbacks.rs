use crate::frontend_impl::*;

use crate::result::{Error, Result};
use crate::util;
use ffi::CString;
use libloading::Library;
use libretro_sys::*;
use once_cell::sync::Lazy;
use std::os::unix::ffi::OsStrExt;
use std::path::Path;
use std::{fs, mem::MaybeUninit};

use rgb565::Rgb565;

use std::ffi;

use tracing::{error, info, warn};


pub(crate) unsafe extern "C" fn environment_callback(
	environment_command: u32,
	data: *mut ffi::c_void,
) -> bool {
	match environment_command {
		ENVIRONMENT_GET_CAN_DUPE => {
			*(data as *mut bool) = true;
			return true;
		}

		ENVIRONMENT_SET_PIXEL_FORMAT => {
			let _pixel_format = *(data as *const ffi::c_uint);
			let pixel_format = PixelFormat::from_uint(_pixel_format).unwrap();
			FRONTEND_IMPL.pixel_format = pixel_format;
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
				}
				Err(_err) => {
					// Maybe notify about this.
					return false;
				}
			}
		},

		ENVIRONMENT_GET_VARIABLE_UPDATE => {
			// We currently pressent no changed variables to the core.
			*(data as *mut bool) = false;
			return true;
		}

		_ => {
			//error!("Environment callback called with currently unhandled command: {environment_command}");
		}
	}

	false
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

	match FRONTEND_IMPL.pixel_format {
		PixelFormat::RGB565 => {
			let pixel_data_slice = std::slice::from_raw_parts(
				pixels as *const u16,
				(width * height) as usize,
			);

			// Resize the pixel buffer if we need to
			if (width * height) as usize != FRONTEND_IMPL.converted_pixel_buffer.len() {
				info!("Resizing RGB565 -> RGBA buffer");
				FRONTEND_IMPL
					.converted_pixel_buffer
					.resize((width * height) as usize, 0);
			}

			/* 
			for x in 0..width as usize {
				for y in 0..height as usize {
					let rgb = Rgb565::from_rgb565(pixel_data_slice[y * width as usize + x]);
					let comp = rgb.to_rgb888_components();

					// Finally save the pixel data in the result array as an XRGB8888 value
					FRONTEND_IMPL.converted_pixel_buffer[y * width as usize + x] =
						((comp[0] as u32) << 16) | ((comp[1] as u32) << 8) | (comp[2] as u32);
				}
			}
			*/

			for y in 0..(width * height) as usize {
				let rgb = Rgb565::from_rgb565(pixel_data_slice[y]);
				let comp = rgb.to_rgb888_components();

				// Finally save the pixel data in the result array as an XRGB8888 value
				FRONTEND_IMPL.converted_pixel_buffer[y] =
					((comp[0] as u32) << 16) | ((comp[1] as u32) << 8) | (comp[2] as u32);
			}
			
			if let Some(update_callback) = &mut FRONTEND_IMPL.video_update_callback {
				update_callback(&FRONTEND_IMPL.converted_pixel_buffer.as_slice());
			}
		}
		

		// We should just be able to make a slice to &[u32] and pass that directly,
		// unless there's any more stupid pitch trickery.
		_ => panic!("Unhandled pixel format {:?}", FRONTEND_IMPL.pixel_format),
	}
}

pub(crate)  unsafe extern "C" fn input_poll_callback() {
	// TODO tell consumer about this
	//info!("Input poll called");
}

pub(crate) unsafe extern "C" fn input_state_callback(
	port: ffi::c_uint,
	device: ffi::c_uint,
	index: ffi::c_uint,
	id: ffi::c_uint,
) -> ffi::c_short {
	// For now, consumer should have a say in this though
	0
}

//pub(crate) unsafe extern "C" fn libretro_audio_sample_callback(left: ffi::c_short, right: ffi::c_short) {
	//info!("audio sample called");
//}

pub(crate) unsafe extern "C" fn audio_sample_batch_callback(
	// Is actually a [[l, r]] pair.
	samples: *const i16,
	frames: usize,
) -> usize {
	//info!("Audio batch called");
	frames
}
