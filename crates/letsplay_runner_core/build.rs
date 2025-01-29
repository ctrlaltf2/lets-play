use protobuf_codegen::CodeGen;

fn main() {
	// Generate code from protobufs
    let _ = CodeGen::new()
        .inputs([
			"protobuf/rpc_client.proto",
			"protobuf/rpc_server.proto",
        ])
        .generate_and_compile()
        .unwrap();
}
