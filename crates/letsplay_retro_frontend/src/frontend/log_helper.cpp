#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "string_pool.hpp"

using LibRetroLogLevel = std::uint32_t;

letsplay::StringPool TheLoggerStringPool;

extern "C" {

/// This function is defined in Rust and recieves our formatted log messages.
void letsplay_retro_frontend_log(LibRetroLogLevel level, const char* buf);

/// This helper function is given to Rust code to implement the libretro logging
/// (because it's a C-varadic function; that requires nightly/unstable Rust)
///
/// By implementing it in C++, we can dodge all that and keep using stable rustc.
void letsplay_retro_frontend_libretro_log(LibRetroLogLevel level, const char* format, ...) {
	va_list val;

	// First query how long the formatted string would be.
	va_start(val, format);
	auto formatLength = std::vsnprintf(nullptr, 0, format, val);
	if(formatLength == -1)
		return;
	va_end(val);

	// Try and find (possibly allocating) a string on the string pool with that length.
	// If allocating a string fails, give up entirely.
	auto* pString = TheLoggerStringPool.GetString(formatLength);
	if(pString == nullptr)
		return;

	auto ptr = pString->GetPointer();

	va_start(val, format);
	// Format the string
	std::vsnprintf(ptr, formatLength, format, val);
	va_end(val);

	// Remove the last newline and replace it with a null terminator, since
	// Tracing will write a newline on its own.
	if(ptr[formatLength - 1] == '\n')
		ptr[formatLength - 1] = '\0';

	// Call the Rust-side reciever.
	letsplay_retro_frontend_log(level, ptr);

	// Return the string back to the pool, adding it to the list of
	// now freed strings that we can re-use.
	TheLoggerStringPool.ReturnString(pString);
}
}
