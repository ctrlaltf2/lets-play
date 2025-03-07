use cc;

fn main() {
	let mut build = cc::Build::new();

	// TODO: if any more files get included here (I'm debating doing so)
	// switch to cmake-rs or something, it's borderline unbearble to have to
	// perform dummy modifications to build.rs just to rebuild the C++ code here
	build
		.emit_rerun_if_env_changed(true)
		.cpp(true)
		.std("c++20")
		.file("src/frontend/string_pool.cpp")
		.file("src/frontend/log_helper.cpp")
		.compile("letsplay_retro_frontend_cxx");
}
