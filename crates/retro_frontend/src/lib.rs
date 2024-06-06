//! A libretro frontend as a reusable library crate.

#![feature(c_variadic)]

/// Re-export
pub use libretro_sys;

mod frontend_impl;
mod libretro_callbacks;
mod libretro_log;

pub mod core;

pub mod util;
pub mod frontend;
pub mod result;
