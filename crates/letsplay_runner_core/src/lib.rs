#[cfg(feature = "client")]
pub mod client;

pub mod shared;

pub use tokio as tokio;

// These names, and the `crate::`
// path are hardcoded in the generator :(

pub(crate) mod input_capnp {
    include!(concat!(env!("OUT_DIR"), "/proto/shared/input_capnp.rs"));
}

pub(crate) mod rpc_client_capnp {
    include!(concat!(env!("OUT_DIR"), "/proto/runner/rpc_client_capnp.rs"));
}

pub(crate) mod rpc_server_capnp {
    include!(concat!(env!("OUT_DIR"), "/proto/runner/rpc_server_capnp.rs"));
}


#[macro_export]
#[cfg(feature = "client")]
/// Creates the boilerplate main() used for runner clients.
/// # Notes
/// We force a `current_thread` runtime because Tokio will not be forced
/// to do any super crazy 40GB/s I/O or something. We run seperate threads
/// tasked with doing the compute/GPU heavy things, and do async I/O on the
/// main thread, since it won't be overloaded probably ever.
macro_rules! client_main {
	($($impl_type:tt)*) => {
		#[$crate::tokio::main(flavor = "current_thread")]
		async fn main() -> anyhow::Result<()> {
			let game = $($impl_type)*::new();
			Ok(client::main(game).await?)
		}
	};
}
