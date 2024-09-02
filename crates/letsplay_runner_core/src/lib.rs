pub mod client;

#[macro_export]
/// Creates the boilerplate main() used for runner clients.
macro_rules! client_main {
	($($impl_type:tt)*) => {
		#[tokio::main(flavor = "current_thread")]
		async fn main() -> anyhow::Result<()> {
			let game = Box::new($($impl_type)*::new());
			Ok(client::main(game).await?)
		}
	};
}
