//! Callbacks for libretro
use crate::libretro_sys_new::*;
use crate::{frontend::*, libretro_log, util};

use std::ffi;

use tracing::{debug, error};

use letsplay_core::alloc::alloc_boxed_slice;

/// The currently running frontend.
///
/// # Safety
/// Libretro itself is not thread safe, so we do not try and pretend that we are.
/// Only one instance of Frontend can be active in an application.
pub(crate) static mut FRONTEND: *mut Frontend = std::ptr::null_mut();

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

				let slice =
					util::terminated_array(desc.types, |item| item.desc.is_null() && item.id == 0);

				for p in slice {
					debug!(
						"desc type: {:?} (name is {})",
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

			match hw_render_context_type {
				HwContextType::OpenGL | HwContextType::OpenGLCore => {
					let init_data = (*(*FRONTEND).interface).hw_gl_init();

					if init_data.is_none() {
						return false;
					}

					let init_data_unwrapped = init_data.unwrap();

					hw_render.get_current_framebuffer = hw_gl_get_framebuffer;
					hw_render.get_proc_address =
						std::mem::transmute(init_data_unwrapped.get_proc_address);

					// Reset the context now that we have given the core the correct information
					(hw_render.context_reset)();

					tracing::info!(
						"{:?} HWContext initalized successfully",
						hw_render_context_type
					);
				}

				_ => {
					error!(
						"Core is trying to request an context type we don't support ({:?}), failing",
						hw_render_context_type
					);
					return false;
				}
			}

			// Once we have initalized HW rendering, we do not need a conversion buffer.
			(*FRONTEND).converted_pixel_buffer = None;

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
					match (*FRONTEND).variables.get_mut(key) {
						Some(value) => {
							let value_str = value.get_value();
							libretro_variable.value = value_str.as_ptr() as *const i8;
							return true;
						}

						None => {
							// value doesn't exist, tell the core that
							libretro_variable.value = std::ptr::null();
							return false;
						}
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

				match CoreVariable::parse(value) {
					Ok(value) => {
						(*FRONTEND).variables.insert(key.to_string(), value);
					}
					Err(error) => {
						tracing::error!("Error parsing core variable {key}: {:?}", error);
					}
				}
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
	// This is how frame duplication is signaled, even w/ HW rendering (or some cores are just badly written.
	// I mean I've already had to baby cores enough!).
	// One word: Bleh.
	if pixels.is_null() {
		return;
	}

	if (*FRONTEND).fb_width != width || (*FRONTEND).fb_height != height {
		// bleh
		(*FRONTEND).fb_width = width;
		(*FRONTEND).fb_height = height;

		(*(*FRONTEND).interface).video_resize(width, height);
	}

	// This means that hardware context was used to render and we need to get it via that.
	if pixels == (-1i64 as *const ffi::c_void) {
		(*(*FRONTEND).interface).video_update_gl();
		return;
	}

	(*FRONTEND).fb_pitch =
		pitch as u32 / util::bytes_per_pixel_from_libretro((*FRONTEND).pixel_format);

	let pitch = (*FRONTEND).fb_pitch as usize;

	// Resize or allocate the conversion buffer if we need to
	if (*FRONTEND).converted_pixel_buffer.is_none() {
		(*FRONTEND).converted_pixel_buffer = Some(alloc_boxed_slice(pitch * height as usize));
	} else {
		let buffer = (*FRONTEND).converted_pixel_buffer.as_ref().unwrap();
		if (pitch * height as usize) as usize != buffer.len() {
			(*FRONTEND).converted_pixel_buffer = Some(alloc_boxed_slice(pitch * height as usize));
		}
	}

	let buffer = (*FRONTEND).converted_pixel_buffer.as_mut().unwrap();

	// Depending on the pixel format, do the appropiate conversion to XRGB8888.
	//
	// We use XRGB8888 as a standard format since it's more convinent to work with
	// and also directly works with NVENC. (Other encoders will require GPU-side kernels to
	// conv. to YUV or NV12, but that's a battle for later.)
	match (*FRONTEND).pixel_format {
		PixelFormat::ARGB1555 => {
			let pixel_data_slice = std::slice::from_raw_parts(
				pixels as *const u16,
				(pitch * height as usize) as usize,
			);

			for x in 0..pitch as usize {
				for y in 0..height as usize {
					let pixel = pixel_data_slice[y * pitch as usize + x];

					// We currently ignore the alpha bit
					let comp = (
						(pixel & 0x7c00) as u8,
						((pixel & 0x3e0) >> 8) as u8,
						(pixel & 0x1f) as u8,
					);

					// Finally save the pixel data in the result array as an XRGB8888 value
					buffer[y * pitch as usize + x] = (255u32 << 24)
						| ((comp.2 as u32) << 16)
						| ((comp.1 as u32) << 8)
						| (comp.0 as u32);
				}
			}
		}

		PixelFormat::RGB565 => {
			let pixel_data_slice = std::slice::from_raw_parts(
				pixels as *const u16,
				(pitch * height as usize) as usize,
			);

			for x in 0..pitch as usize {
				for y in 0..height as usize {
					let pixel = pixel_data_slice[y * pitch as usize + x];
					let comp: (u8, u8, u8) = (
						((pixel >> 11 & 0x1f) * 255 / 0x1f) as u8,
						((pixel >> 5 & 0x3f) * 255 / 0x3f) as u8,
						((pixel & 0x1f) * 255 / 0x1f) as u8,
					);

					buffer[y * pitch as usize + x] = (255u32 << 24)
						| ((comp.2 as u32) << 16)
						| ((comp.1 as u32) << 8)
						| (comp.0 as u32);
				}
			}
		}

		PixelFormat::ARGB8888 => {
			let pixel_data_slice = std::slice::from_raw_parts(
				pixels as *const u32,
				(pitch * height as usize) as usize,
			);

			// FIXME: could be simd-ified to do this across 4 or 8 pixels at once per line
			// practically speaking however, it's *probably* not worth doing so because
			// cores that might take advantage of such a optimized cpu kernel
			// will probably have hardware rendering support.
			// (therefore, we don't need to copy or change the format of anything)
			for x in 0..pitch as usize {
				for y in 0..height as usize {
					let pixel = pixel_data_slice[y * pitch as usize + x];

					let comp = (
						((pixel & 0xff_00_00_00) >> 24) as u8,
						((pixel & 0x00_ff_00_00) >> 16) as u8,
						((pixel & 0x00_00_ff_00) >> 8) as u8,
						(pixel & 0x00_00_00_ff) as u8,
					);

					buffer[y * pitch as usize + x] = (255u32 << 24)
						| ((comp.3 as u32) << 16)
						| ((comp.2 as u32) << 8)
						| (comp.1 as u32);
				}
			}
		}
	}

	(*(*FRONTEND).interface).video_update(&buffer[..], pitch as u32);
}

pub(crate) unsafe extern "C" fn input_poll_callback() {
	(*(*FRONTEND).interface).input_poll();
}

pub(crate) unsafe extern "C" fn input_state_callback(
	port: ffi::c_uint,
	device_type: ffi::c_uint,
	index: ffi::c_uint, // not used?
	button_id: ffi::c_uint,
) -> ffi::c_short {
	match (*FRONTEND).input_devices.get(&port) {
		Some(input_device) => {
			let d = input_device.as_mut().unwrap();
			if d.device_type_compatible(device_type) {
				return d.get_index(index, button_id);
			}

			0
		}

		None => 0,
	}
}

pub(crate) unsafe extern "C" fn audio_sample_callback(_left: i16, _right: i16) {
	// FIXME: we should batch these internally and then call the sample callback
	// (wouldn't be too hard..)
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
