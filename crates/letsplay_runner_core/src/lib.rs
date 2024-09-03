pub mod client;

#[macro_export]
/// Creates the boilerplate main() used for runner clients.
/// # Notes
/// We force a `current_thread` runtime because Tokio will not be forced
/// to do any super crazy 40GB/s I/O or something. We run seperate threads
/// tasked with doing the compute/GPU heavy things, and do async I/O on the
/// main thread, since it won't be overloaded probably ever.
macro_rules! client_main {
	($($impl_type:tt)*) => {
		#[tokio::main(flavor = "current_thread")]
		async fn main() -> anyhow::Result<()> {
			let game = Box::new($($impl_type)*::new());
			Ok(client::main(game).await?)
		}
	};
}
