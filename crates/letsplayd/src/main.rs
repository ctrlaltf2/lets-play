use retro_frontend::Frontend;

fn main() {
	let mut fe = Frontend::new();

	fe.load_core("./cores/gambatte_libretro.so")
		.expect("Core should have loaded");

	// We could now do interesting stuff
	println!("Core loaded!");
}
