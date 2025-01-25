//! Selective additional (2019+) updates on top of the existing libretro_sys crate.

pub use libretro_sys::*;
use std::ffi;

/// Represents a bitmask that describes the state of all [DEVICE_ID_JOYPAD] button constants,
/// rather than the state of a single button.
pub const DEVICE_ID_JOYPAD_MASK: libc::c_uint = 256;

/// Defines overrides which modify frontend handling of specific content file types.
/// An array of [SystemContentInfoOverride] is passed to [RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE]
#[repr(C)]
pub struct SystemContentInfoOverride {
	pub extensions: *const ffi::c_char,
	pub need_fullpath: bool,
	pub persistent_data: bool,
}

#[repr(C)]
pub struct GameInfoExt {
	pub full_path: *const ffi::c_char,
	pub archive_path: *const ffi::c_char,
	pub archive_file: *const ffi::c_char,
	pub dir: *const ffi::c_char,
	pub name: *const ffi::c_char,
	pub ext: *const ffi::c_char,
	pub meta: *const ffi::c_char,

	pub data: *const ffi::c_void,
	pub size: usize,

	/// True if loaded content file is inside a compressed archive
	pub file_in_archive: bool,

	pub persistent_data: bool,
}

/// *const [SystemContentInfoOverride] (array, NULL extensions terminates it)
pub const RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE: ffi::c_uint = 65;

/// *const *const [GameInfoExt]
pub const RETRO_ENVIRONMENT_GET_GAME_INFO_EXT: ffi::c_uint = 66;
