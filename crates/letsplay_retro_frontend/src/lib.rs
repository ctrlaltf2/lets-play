//! A libretro frontend as a reusable library crate.

mod libretro_log;

pub mod libretro_sys_new;

pub mod input_devices;
pub mod util;

pub mod frontend;
pub mod result;

// re-export some of our useful interface
pub use frontend::{CoreVariable, Frontend, FrontendInterface};
