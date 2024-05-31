//! A libretro frontend as a reusable library crate

mod frontend_impl;

pub mod frontend;
pub use frontend::*;

pub mod result;
