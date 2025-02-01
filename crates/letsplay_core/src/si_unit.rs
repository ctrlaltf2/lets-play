//! Because I'm big stupid with SI units

/// Implements named SI unit types.
macro_rules! implement_si_unit {
	($name:ident, $unit_name:literal, $s:ident, $calc:expr) => {
		#[doc="The "]
		#[doc=$unit_name]
		#[doc=" SI unit."]
		pub struct $name(pub usize);

		impl $name {
			// At some point figure out how to get plurals to work.
			// I can't be bothered to atm
			#[doc="Converts from "]
			#[doc=$unit_name]
			#[doc="to bytes."]
			pub const fn in_bytes(&$s) -> usize {
				$calc
			}
		}
	};
}

implement_si_unit!(Kb, "kilobit", self, self.0 * 125);
implement_si_unit!(Mb, "megabit", self, (self.0 / 8) * 1000000);

implement_si_unit!(KiB, "kibibyte", self, self.0 * 1024);
implement_si_unit!(KB, "kilobyte", self, self.0 * 1000);

implement_si_unit!(MB, "megabyte", self, self.0 * (1000 * 1000));
implement_si_unit!(MiB, "mebibyte", self, self.0 * (1024 * 1024));
