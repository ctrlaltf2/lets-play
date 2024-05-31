use retro_frontend::frontend;

fn main() {
	frontend::load_core("./cores/gambatte_libretro.so")
		.expect("Core should have loaded");

	// We could now do interesting stuff
	println!("Core loaded!");
}
