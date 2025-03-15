@0xe702e8d03d00bedb;

# These are messages sent from letsplayd to a runner, whether running
# locally or using QUIC.

# FIXME: The input types should probably be shared between the general protocol.

using Input = import "../shared/input.capnp";

struct ServerMessage {

    struct SetSuspend {
        # Set the suspend flag.
        # 
        # The suspend flag is used by the runner to suspend emulation,
        # which can be used to reduce resource usage/pause the gameplay
        # while the emulator node is empty after a certain period of time.

        suspendFlag @0 :Bool;
        # The new value of the suspend flag.
    }

    struct InputDeviceEvent {
        # A input device-related event.

        struct Update {
            # Message to update input state.

            input :union {
                # Input union. If the union sent is the wrong type
                # for the device at that index, the message will be ignored.

                joyInput @0 :Input.JoyInput;
                # Joystick input.

                kbMouseInput @1 :Input.KBMouseInput;
                # Keyboard & mouse input update.
            }
        }

        index @0 :UInt8;
        # The device index the event is for.

        event :union {
            plugIn @1 :Input.DeviceType;
            # Plug in the device with the given device type into the 
            # given device index (if it is valid).

            unplug @2 :Void;
            # Unplug the device from the given device index (if it is valid)

            update @3: Update;
            # Update the given device.
        }

    }

    message :union {
        shutdown @0 :Void;
        # Signal to shut down the runner.

        resetGame @1 :Void;
        # Reset the game currently running.

        setSuspend @2 :SetSuspend;
        # Set the suspend flag of the runner.

        inputDeviceEvent @3 :InputDeviceEvent;
        # A input device event.

        requestStreamReset @4 :Void;
        # Request a reset of the video stream.
        # Typically used when a client connects, so we
        # can immediately give them keyframe packets.
    }
}