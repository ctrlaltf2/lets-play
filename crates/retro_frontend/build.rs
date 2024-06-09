use cc;

fn main() {
	let mut build = cc::Build::new();

	build
		.emit_rerun_if_env_changed(true)
		.cpp(true)
		.std("c++20")
		.file("src/libretro_log_helper.cpp")
		.compile("retro_log_helper");
}
