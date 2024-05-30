use thiserror::Error;

#[derive(Error, Debug)]
pub enum Error {
	#[error("error while loading core library")]
	LibError(#[from] libloading::Error),

	#[error("expected core API version {expected}, but core returned {got}")]
	InvalidLibRetroAPI { expected: u32, got: u32 },
}

pub type Result<T> = std::result::Result<T, Error>;
