//! A libretro frontend as a reusable library crate.

#![feature(c_variadic)]

mod frontend_impl;
mod libretro_callbacks;
mod libretro_log;

pub mod libretro_sys_new;

pub mod core;

pub mod util;
pub mod frontend;
pub mod result;
