//! A libretro frontend as a reusable library crate.

pub mod input_devices;
mod util;

pub mod frontend;
pub mod result;

// re-export some of our useful interface
pub use frontend::{CoreVariable, Frontend, FrontendInterface};
pub use result::*;

// re-export sys crate
pub use letsplay_libretro_sys as sys;
