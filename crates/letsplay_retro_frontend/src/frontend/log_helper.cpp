#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "string_pool.hpp"

using LibRetroLogLevel = std::uint32_t;

namespace {
	/// The libretro logger's string pool. Used to hold strings on the heap
	/// without leaking them, but still allowing re-use of previously allocated strings.
	letsplay::StringPool TheLoggerStringPool;
} // namespace

extern "C" {

/// This function is defined in Rust and actually outputs the log messages.
void letsplay_retro_frontend_log(LibRetroLogLevel level, const char* buf);

/// This helper function is given to libretro cores in the log interface to call.
///
/// We do all the formatting here, since we can't use C-varadics with our MSRV, and then
/// pass the formatted string to Rust code.
void letsplay_retro_frontend_libretro_log(LibRetroLogLevel level, const char* format, ...) {
	va_list val;
	std::int32_t formatLength = 0;

	// By passing nullptr to vsnprintf, it will return the exact buffer size needed
	// to hold the output string.
	va_start(val, format);
	formatLength = std::vsnprintf(nullptr, 0, format, val);
	if(formatLength == -1)
		return;
	va_end(val);

	// Try and find (possibly allocating) a string on the string pool with that length.
	// If allocating a string fails, we simply give up.
	auto pooledString = TheLoggerStringPool.GetString(formatLength);
	if(!pooledString.has_value())
		return;

	// We got a string. Let's format into it.
	auto pooledStringMemory = static_cast<letsplay::StringPool::PooledString&>(*pooledString).GetPointer();
	
	va_start(val, format);
	std::vsnprintf(pooledStringMemory, formatLength, format, val);
	va_end(val);

	// Remove the last newline and replace it with a null terminator, since
	// Tracing will write a newline on its own.
	if(pooledStringMemory[formatLength - 1] == '\n')
		pooledStringMemory[formatLength - 1] = '\0';

	// Call the Rust-side reciever function which will log the message.
	letsplay_retro_frontend_log(level, pooledStringMemory);
}
}
