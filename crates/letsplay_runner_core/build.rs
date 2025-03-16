use std::{fmt::Display, path::Path};

use capnpc;

fn compile_capnp<P: AsRef<Path> + Display>(path: P) {
	// Compile shared defs
	capnpc::CompilerCommand::new()
		.file(&path)
		.run()
		.expect(&format!("compiling {} failed", path));
}

fn main() {
	// Compile shared defs
	compile_capnp("proto/shared/input.capnp");

	// Compile RPC defs
	compile_capnp("proto/runner/rpc_client.capnp");
	compile_capnp("proto/runner/rpc_server.capnp");
}
