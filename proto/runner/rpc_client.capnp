@0xe702e8d03d00bedb;

# These are messages sent from letsplayd to a runner, whether running
# locally or using QUIC.

# FIXME: The input types should probably be shared between the general protocol.

enum InputDeviceType {
    # Input device type enumeration.

    joypad @0;
    # A joypad without analog sticks.

    analogJoypad @1;
    # A joypad *with* analog sticks.

    keyboardAndMouse @2;
    # A keyboard and mouse.
}

struct JoyInput {
    # A input for a Joystick.

    struct Stick {
        # A analog stick input.
        #
        # Note that all fields here pack their floats.
        # The packing looks like looks like:
        #
        # encode(x: f32) = (u16)(x * 65535.0f),
        # encode(x: u16) = (x / 65535.0f)
        #
        # This is probably *very* extra, but the less bytes the better.

        x @0 :UInt16;
        # The X axis of the joystick.

        y @1 :UInt16;
        # The Y axis of the joystick.
    }

    buttons @0 :UInt32;
    # Joystick button inputs.

    leftStick @1 :Stick;
    # Left stick input.

    rightStick @2 :Stick;
    # Right stick input.
}

struct KBMouseInput {
    # Keyboard/mouse input.

    struct Keyboard {
        struct Key {
            keySym @0 :UInt32;
            # The keysym.

            pressed @1 :Bool;
            # Pressed.
        }

        keys @0 :List(Key);
        # All pressed/released keys.
    }

    struct Mouse {
        x @0 :UInt16;
        # X position of the mouse.

        y @1 :UInt16;
        # Y position of the mouse.

        buttons @2 :UInt16;
        # Buttons pressed.
    }

    inputType :union {
        keyboard @0 :Keyboard;
        # Keyboard input.

        mouse @1 :Mouse;
        # Mouse input.
    }
}

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

                joyInput @0 :JoyInput;
                # Joystick input.

                kbMouseInput @1 :KBMouseInput;
                # Keyboard & mouse input update.
            }
        }

        index @0 :UInt8;
        # The device index the event is for.

        event :union {
            plugIn @1 :InputDeviceType;
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