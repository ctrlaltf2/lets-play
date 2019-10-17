# Set CMake variables:
#   * HUNTER_${PACKAGE_NAME}_VERSION

# Usage:
#   hunter_default_version(Foo VERSION 1.0.0)
#   hunter_default_version(Boo VERSION 1.2.3z)

include(hunter_user_error)

# NOTE: no names with spaces!

hunter_config(websocketpp VERSION 0.8.1-p0)
hunter_config(Boost VERSION 1.69.0-p0)
