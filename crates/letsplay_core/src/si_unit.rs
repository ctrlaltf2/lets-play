//! Because I'm big stupid with SI units

/// Megabit
pub struct Mb(usize);

impl Mb {
	pub const fn new(unit: usize) -> Self {
		Self(unit)
	}

	pub const fn in_bytes(&self) -> usize {
		(self.0 / 8) * 1000000
	}
}

/// Megabyte
pub struct MB(usize);

impl MB {
	pub const fn new(unit: usize) -> Self {
		Self(unit)
	}

	pub const fn in_bytes(&self) -> usize {
		self.0 * (1000usize * 1000usize)
	}
}
