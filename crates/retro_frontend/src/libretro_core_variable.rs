//! Helpers for dealing with Libretro configuration values.

use serde::{Deserialize, Serialize};
use std::ffi::CString;

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
	pub fn parse(str: &str) -> Self {
		let string = str.to_string();

		match string.find(';') {
			Some(index) => {
				let name = &string[0..index];

				// FIXME: Instead of panicing, we should return a Result or the like
				if string.chars().nth(index + 1).unwrap() != ' ' {
					panic!("Improperly formatted core variable");
				}

				let raw_choices = string[index + 2..].to_string();
				let choices = raw_choices.split('|').map(|s| s.to_string()).collect();

				Self {
					description: name.to_string(),
					choices: choices,
					value: None,
					c_value: None,
				}
			}
			None => panic!("??? Couldn't find"),
		}
	}

	/// Gets this variable's value
	pub fn get_value(&mut self) -> &CString {
		let rust_value = if self.value.is_some() {
			self.value.as_ref().unwrap()
		} else {
			&self.choices[0]
		};

		if self.c_value.is_none() {
			self.c_value = Some(CString::new(rust_value.as_bytes()).expect("aaa"));
		}
		self.c_value.as_ref().unwrap()
	}

	/// Sets a new value
	pub fn set_value(&mut self, value: &String) {
		self.value = Some(value.clone());
		self.c_value = None;
	}
}
