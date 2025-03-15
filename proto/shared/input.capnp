@0x9cec1e26499ac72d;

# This file contains shared definitions for input-related stuff.

enum DeviceType {
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