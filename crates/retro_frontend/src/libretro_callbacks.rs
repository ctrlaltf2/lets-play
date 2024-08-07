//! Callbacks for libretro
use crate::{frontend::*, libretro_log, util};
use crate::{libretro_core_variable, libretro_sys_new::*};

use rgb565::Rgb565;

use std::ffi;

use tracing::{debug, error};

/// This function is used with HW OpenGL cores to transfer the current FBO's ID.
unsafe extern "C" fn hw_gl_get_framebuffer() -> usize {
	(*FRONTEND).gl_fbo_id as usize
}

pub(crate) unsafe extern "C" fn environment_callback(
	environment_command: u32,
	data: *mut ffi::c_void,
) -> bool {
	match environment_command {
		ENVIRONMENT_GET_LOG_INTERFACE => {
			*(data as *mut LogCallback) = libretro_log::LOG_INTERFACE.clone();
			return true;
		}

		ENVIRONMENT_SET_PERFORMANCE_LEVEL => {
			let level = *(data as *const ffi::c_uint);
			debug!("Core is performance level {level}");
			return true;
		}

		ENVIRONMENT_SET_CONTROLLER_INFO => {
			let ptr = data as *const ControllerInfo;

			let slice = util::terminated_array(ptr, |item| {
				return item.num_types == 0 && item.types.is_null();
			});

			for desc in slice {
				debug!("{:?}", desc);

				for i in 0..desc.num_types as usize {
					let p = desc.types.add(i).as_ref().unwrap();
					debug!(
						"type {i} = {:?} (name is {})",
						p,
						std::ffi::CStr::from_ptr(p.desc).to_str().unwrap()
					);
				}
			}

			return true;
		}

		ENVIRONMENT_SET_INPUT_DESCRIPTORS => {
			let ptr = data as *const InputDescriptor;

			let slice = util::terminated_array(ptr, |item| {
				return item.description.is_null();
			});

			debug!("{} input descriptor entries", slice.len());

			for desc in slice {
				debug!("Descriptor {:?}", desc);
			}

			return true;
		}

		ENVIRONMENT_GET_CAN_DUPE => {
			*(data as *mut bool) = true;
			return true;
		}

		ENVIRONMENT_GET_SYSTEM_DIRECTORY => {
			*(data as *mut *const ffi::c_char) = (*FRONTEND).system_directory.as_ptr();
			return true;
		}

		ENVIRONMENT_GET_SAVE_DIRECTORY => {
			*(data as *mut *const ffi::c_char) = (*FRONTEND).save_directory.as_ptr();
			return true;
		}

		ENVIRONMENT_SET_PIXEL_FORMAT => {
			let _pixel_format = *(data as *const ffi::c_uint);
			let pixel_format = PixelFormat::from_uint(_pixel_format).unwrap();
			(*FRONTEND).pixel_format = pixel_format;
			return true;
		}

		ENVIRONMENT_SET_GEOMETRY => {
			if data.is_null() {
				return false;
			}

			let geometry = (data as *const GameGeometry).as_ref().unwrap();

			(*FRONTEND).fb_width = geometry.base_width;
			(*FRONTEND).fb_height = geometry.base_height;

			(*(*FRONTEND).interface).video_resize(geometry.base_width, geometry.base_height);
			return true;
		}

		ENVIRONMENT_SET_HW_RENDER => {
			let hw_render = (data as *mut HwRenderCallback).as_mut().unwrap();

			let hw_render_context_type =
				HwContextType::from_uint(hw_render.context_type).expect("Uh oh!");

			if hw_render_context_type != HwContextType::OpenGL
				&& hw_render_context_type != HwContextType::OpenGLCore
			{
				error!(
					"Core is trying to request an context type we don't support ({:?}), failing",
					hw_render_context_type
				);
				return false;
			}

			let init_data = (*(*FRONTEND).interface).hw_gl_init();

			hw_render.get_current_framebuffer = hw_gl_get_framebuffer;
			hw_render.get_proc_address = std::mem::transmute(init_data.get_proc_address);

			// reset context
			(hw_render.context_reset)();

			// Once we have initalized HW rendering any data here doesn't matter and isn't needed.
			(*FRONTEND).converted_pixel_buffer.clear();

			return true;
		}

		ENVIRONMENT_GET_VARIABLE => {
			// Make sure the core actually is giving us a pointer to a *Variable
			// so we can fill it in.
			if data.is_null() {
				return false;
			}

			let libretro_variable = (data as *mut Variable).as_mut().unwrap();

			match ffi::CStr::from_ptr(libretro_variable.key).to_str() {
				Ok(key) => {
					if (*FRONTEND).variables.contains_key(key) {
						let value = (*FRONTEND).variables.get_mut(key).unwrap();
						let value_str = value.get_value();
						libretro_variable.value = value_str.as_ptr() as *const i8;
						return true;
					} else {
						// value doesn't exist, tell the core that
						libretro_variable.value = std::ptr::null();
						return false;
					}
				}
				Err(err) => {
					error!(
						"Core gave an invalid key for ENVIRONMENT_GET_VARIABLE: {:?}",
						err
					);
					return false;
				}
			}
		}

		ENVIRONMENT_GET_VARIABLE_UPDATE => {
			// We currently pressent no changed variables to the core.
			// TODO: this will change
			*(data as *mut bool) = false;
			return true;
		}

		ENVIRONMENT_SET_VARIABLES => {
			let ptr = data as *const Variable;
			let slice = util::terminated_array(ptr, |item| item.key.is_null());

			// populate variables hashmap
			for var in slice {
				let key = std::ffi::CStr::from_ptr(var.key).to_str().unwrap();
				let value = std::ffi::CStr::from_ptr(var.value).to_str().unwrap();

				let parsed = libretro_core_variable::CoreVariable::parse(value);

				(*FRONTEND).variables.insert(key.to_string(), parsed);
			}

			// Load settings
			(*FRONTEND).load_settings();

			return true;
		}

		_ => {
			debug!("Environment callback called with currently unhandled command: {environment_command}");
			return false;
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

	if (*FRONTEND).fb_width != width || (*FRONTEND).fb_height != height {
		(*(*FRONTEND).interface).video_resize(width, height);
	}

	// bleh
	(*FRONTEND).fb_width = width;
	(*FRONTEND).fb_height = height;

	if pixels == (-1i64 as *const ffi::c_void) {
		(*(*FRONTEND).interface).video_update_gl();
		return;
	}

	(*FRONTEND).fb_pitch =
		pitch as u32 / util::bytes_per_pixel_from_libretro((*FRONTEND).pixel_format);

	let pitch = (*FRONTEND).fb_pitch as usize;

	match (*FRONTEND).pixel_format {
		PixelFormat::RGB565 => {
			let pixel_data_slice = std::slice::from_raw_parts(
				pixels as *const u16,
				(pitch * height as usize) as usize,
			);

			// Resize the conversion buffer if we need to
			if (pitch * height as usize) as usize != (*FRONTEND).converted_pixel_buffer.len() {
				(*FRONTEND)
					.converted_pixel_buffer
					.resize((pitch * height as usize) as usize, 0);
			}

			for x in 0..pitch as usize {
				for y in 0..height as usize {
					let rgb = Rgb565::from_rgb565(pixel_data_slice[y * pitch as usize + x]);
					let comp = rgb.to_rgb888_components();

					// Finally save the pixel data in the result array as an XRGB8888 value
					(*FRONTEND).converted_pixel_buffer[y * pitch as usize + x] =
						((comp[2] as u32) << 16) | ((comp[1] as u32) << 8) | (comp[0] as u32);
				}
			}

			(*(*FRONTEND).interface)
				.video_update(&(*FRONTEND).converted_pixel_buffer[..], pitch as u32);
		}
		_ => {
			let pixel_data_slice = std::slice::from_raw_parts(
				pixels as *const u32,
				(pitch * height as usize) as usize,
			);

			(*(*FRONTEND).interface).video_update(&pixel_data_slice, pitch as u32);
		}
	}
}

pub(crate) unsafe extern "C" fn input_poll_callback() {
	(*(*FRONTEND).interface).input_poll();
}

pub(crate) unsafe extern "C" fn input_state_callback(
	port: ffi::c_uint,
	device: ffi::c_uint,
	_index: ffi::c_uint, // not used?
	button_id: ffi::c_uint,
) -> ffi::c_short {
	if (*FRONTEND).input_devices.contains_key(&port) {
		let joypad = *(*FRONTEND)
			.input_devices
			.get(&port)
			.expect("How do we get here when contains_key() returns true but the key doen't exist");

		if device == (*joypad).device_type() {
			return (*joypad).get_button(button_id);
		}
	}

	0
}

pub(crate) unsafe extern "C" fn audio_sample_batch_callback(
	// Is actually a [[l, r]] interleaved pair.
	samples: *const i16,
	frames: usize,
) -> usize {
	let slice = std::slice::from_raw_parts(samples, frames * 2);

	// I might not need to give the callback the amount of frames since it can figure it out as
	// slice.len() / 2, but /shrug
	(*(*FRONTEND).interface).audio_sample(slice, frames);
	frames
}
