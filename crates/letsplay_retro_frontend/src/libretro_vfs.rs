//! Libretro VFS

use crate::libretro_sys_new::*;
use std::path;


/// A file handle
struct FileHandle {
	/// The path used to open a file on the host
	real_path: path::PathBuf,
}

//unsafe extern "C" fn libretro_vfs_get_path(stream: *mut )

