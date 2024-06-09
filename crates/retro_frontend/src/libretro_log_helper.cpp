#include <cstdarg>
#include <cstdio>
#include <cstdint>

using LibRetroLogLevel = std::uint32_t;

extern "C" {

	/// This function is defined in Rust and recieves our formatted log messages.
	void libretro_log_recieve(LibRetroLogLevel level, const char* buf);

	/// This helper function is given to Rust code to implement the libretro logging
	/// (because it's a C-varadic function; that requires nightly/unstable Rust)
	///
	/// By implementing it in C++, we can dodge all that and keep using stable rustc.
	void libretro_log(LibRetroLogLevel level, const char* format, ...) {
		char buf[512]{};
		va_list val;

		va_start(val, format);
		auto n = std::vsnprintf(&buf[0], sizeof(buf)-1, format, val);
		va_end(val);

		// Failed to format for some reason, just give up.
		if(n == -1)
			return;

		// Remove the last newline and replace it with a null terminator.
		if(buf[n-1] == '\n')
			buf[n-1] = '\0';

		// Call the Rust-side reciever.
		return libretro_log_recieve(level, &buf[0]);
	}
}
