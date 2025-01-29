//! A libretro frontend as a reusable library crate.

mod libretro_log;

pub mod input_devices;
mod util;

pub mod frontend;
pub mod result;

// re-export some of our useful interface
pub use frontend::{CoreVariable, Frontend, FrontendInterface};
pub use result::*;
