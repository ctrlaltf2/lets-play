//! A libretro frontend as a reusable library crate.

/// Re-export
pub use libretro_sys;

mod frontend_impl;
mod libretro_callbacks;

pub mod core;

pub mod util;
pub mod frontend;
pub mod result;
