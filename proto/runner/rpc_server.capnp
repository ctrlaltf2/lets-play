@0xd675f9afb00c6e94;

# These are messages sent to letsplayd from a runner, whether running
# locally or using QUIC.
#
# TODO: At some point consider using capnp RPC. Unlike the abomination that is protobuf,
# capnp rpc (at least in Rust it's pretty easy, even in C++ it shouldn't be hard either..) is completely
# transport independent. For now, I'm just directly porting the existing protos to capnp.

struct ClientMessage {
    # A message sent to letsplayd from a runner.

    struct Hello {
        # The hello message. Sent to letsplayd as a handshake.

        typeId @0 :Text;
        # A standardized type id, like:
		# 'letsplay.libretro'.
		# Note that only runners part of the Let's Play repository should
		# use 'letsplay.' IDs. External ones should use an alternate one,
        # like their author. Say, if I wrote one, "modeco80.bla" would be what I would use.
        # You don't need to do do any Java reverse FQDN or whatever.

        version @1 :UInt32;
        # The runner protocol version.
        # This is incremented on any bump to
		# the runner protocol that will break compatibility.
		#
        # The current one is '0'.
		# Previously: (past version, date it was discontinued, reason why)

        emulatorId @2 :Text;
        # The emulator ID that letsplayd should use for this runner.
        # Duplicate IDs will cause letsplayd to hangup.
    }
    
    struct AVPacket {
        # Carries a MPEG2-TS transport stream packet to letsplayd
        # to broadcast to connected players.
        #
        # Why MPEG2-TS? Well..
        # - It's fairly easy to demux.
        #
        # - Unlike fMP4, which if I'm understanding properly, requires every
        #   fragment to be decodable on its own (a good property for VOD; not so much
        #   low-latency live streaming), MPEG2-TS does not,
        #   so we can alter the GOP and use i-frames as much as we want.
        #
        # - Unlike it's name suggests, H.264 and H.265 work just fine,
        #   and it is standardized to work.
        # 
        # - It doesn't require me to NIH a ton of muxing stuff.
        #   (Honestly that's most of the decision)

        packetData @0 :Data;
        # The Transport Stream packet data.
    }

    packetType :union {
        # Union for packet type.

        hello @0 :Hello;
        # A hello packet.

        avPacket @1 :AVPacket;
        # A AV data packet.
    }
}