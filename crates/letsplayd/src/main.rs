use retro_frontend::frontend;
use tracing::Level;
use tracing_subscriber::FmtSubscriber;

fn main() {
	// Setup a tracing subscriber
	let subscriber = FmtSubscriber::builder()
		.with_max_level(Level::TRACE)
		.finish();

	tracing::subscriber::set_global_default(subscriber).unwrap();
}
