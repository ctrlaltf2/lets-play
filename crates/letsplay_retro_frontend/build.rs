use cc;

fn main() {
	let mut build = cc::Build::new();

	build
		.emit_rerun_if_env_changed(true)
		.cpp(true)
		.std("c++20")
		.file("src/frontend/log_helper.cpp")
		.compile("letsplay_retro_frontend_cxx");
}
