//! A libretro frontend implemented as a reusable library crate.
//!
//! Designed primarly for easy integration and headless usage.

pub mod input_devices;
mod util;

mod frontend;
mod result;

// re-export some of our useful interface
pub use frontend::*;
pub use result::*;

// re-export sys crate
pub use letsplay_libretro_sys as sys;
