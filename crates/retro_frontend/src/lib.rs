//! A libretro frontend as a reusable library crate.

mod libretro_callbacks;
mod libretro_core_variable;
mod libretro_log;

pub mod libretro_sys_new;

pub mod input_devices;
pub mod util;

pub mod frontend;
pub mod result;
