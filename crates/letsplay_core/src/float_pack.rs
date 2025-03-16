//! Routines for dealing with "packed" floats.
//! 
//! These are floats (ranged from 0.0 -> 1.0) which are encoded
//! as a 16-bit unsigned integer, halving the required storage size.
//! 
//! While the range seems kind of limiting, 
//! the biggest usecase for this is analog joysticks,
//! which already nearly fit into this limitation.
//! 
//! The result is that we half the wire space needed to carry
//! stick information from 16 -> 8 bytes. Pretty nice.

/// Encodes a float into packed repressentation
pub fn encode(source: f32) -> u16 {
    debug_assert!(source <= 1.0, "floats above 1.0 cannot be packed with packed encoding");
    (source * 65535.) as u16
}

/// Decodes a packed float back into its original repressentation
/// (or at least the closest value wrt. precision loss)
pub fn decode(source: u16) -> f32 {
    source as f32 / 65535.
}