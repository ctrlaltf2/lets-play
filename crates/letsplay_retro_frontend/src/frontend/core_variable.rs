//! Helpers for dealing with Libretro configuration values.

use anyhow::anyhow;
use serde::{Deserialize, Serialize};
use std::ffi::CString;

/// A single core variable.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct CoreVariable {
	/// Description of variable
	pub description: String,

	/// possible choices
	pub choices: Vec<String>,

	/// Value. May not be pressent; if so, assume choices[0]
	pub value: Option<String>,

	/// C value. Passed/cached to libretro.
	#[serde(skip)]
	c_value: Option<CString>,
}

impl CoreVariable {
	/// Parses this core variable.
	pub fn parse(str: &str) -> anyhow::Result<Self> {
		let string = str.to_string();

		match string.find(';') {
			Some(index) => {
				let name = &string[0..index];

				if string.chars().nth(index + 1).unwrap() != ' ' {
					return Err(anyhow!("Core variable is improperly formatted"));
				}

				let raw_choices = string[index + 2..].to_string();
				let choices = raw_choices.split('|').map(|s| s.to_string()).collect();

				Ok(Self {
					description: name.to_string(),
					choices: choices,
					value: None,
					c_value: None,
				})
			}
			None => Err(anyhow!("Core variable doesn't have a seperator?")),
		}
	}

	/// Gets this variable's value
	pub fn get_value(&mut self) -> &CString {
		let rust_value = if self.value.is_some() {
			self.value.as_ref().unwrap()
		} else {
			&self.choices[0]
		};

		// TODO: Instead of panicing here we should probably make this function
		// failiable
		if self.c_value.is_none() {
			self.c_value = Some(CString::new(rust_value.as_bytes()).expect("aaa"));
		}
		self.c_value.as_ref().unwrap()
	}

	// TODO: We store all the choices, so set_value *REALLY* should just be
	// set_value(&mut self, index: usize), to avoid the possibility of giving
	// a core invalid values at all.
	//
	// (this does *not* limit API users; core variables are by design gated to a set
	// of possible choices. we can probably add get_choices(&self) -> &Vec<String> which lets you see them)

	/// Sets a new value
	pub fn set_value(&mut self, value: &String) {
		self.value = Some(value.clone());
		self.c_value = None;
	}
}
