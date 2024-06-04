use thiserror::Error;

#[derive(Error, Debug)]
pub enum Error {
	#[error("error while loading core library")]
	LibError(#[from] libloading::Error),

	#[error(transparent)]
	IoError(#[from] std::io::Error),

	#[error("expected core API version {expected}, but core returned {got}")]
	InvalidLibRetroAPI { expected: u32, got: u32 },

	#[error("no core is currently loaded into the frontend")]
	CoreNotLoaded,

	#[error("a core is already loaded into the frontend")]
	CoreAlreadyLoaded,

	#[error("ROM load failed")]
	RomLoadFailed
}

pub type Result<T> = std::result::Result<T, Error>;
