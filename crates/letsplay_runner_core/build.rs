use capnpc;

fn main() {
	// Compile RPC defs
	capnpc::CompilerCommand::new()
		.file("rpc/rpc_server.capnp")
		.run()
		.expect("compiling rpc server");

	capnpc::CompilerCommand::new()
		.file("rpc/rpc_client.capnp")
		.run()
		.expect("compiling rpc client");
}
