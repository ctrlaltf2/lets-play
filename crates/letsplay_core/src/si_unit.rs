//! Because I'm big stupid with SI units

/// Implements named SI unit types.
macro_rules! implement_si_unit {
	($name:ident, $s:ident, $calc:expr) => {
		pub struct $name(pub usize);

		impl $name {
			/// Converts this unit to bytes.
			pub const fn in_bytes(&$s) -> usize {
				$calc
			}
		}
	};
}

implement_si_unit!(Kb, self, self.0 * 125);
implement_si_unit!(KiB, self, self.0 * 1024);
implement_si_unit!(KB, self, self.0 * 1000);

implement_si_unit!(MbNew, self, (self.0 / 8) * 1000000);
implement_si_unit!(MBNew, self, self.0 * (1000 * 1000));
implement_si_unit!(MiB, self, self.0 * (1024 * 1024));

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
