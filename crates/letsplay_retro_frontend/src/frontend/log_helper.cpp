#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <new>

using LibRetroLogLevel = std::uint32_t;

// set to 1 to enable debug messages for the string pool
#define STRINGPOOL_DEBUG 0

#if STRINGPOOL_DEBUG
	#define STRINGPOOL_DEBUG_PRINTF(fmt, ...) printf("stringpool debug: " fmt, ##__VA_ARGS__)
#else
	#define STRINGPOOL_DEBUG_PRINTF(fmt, ...)
#endif

/// A very simple string pool implemented using a linked list as a very bad freelist.
struct StringPool {
	/// The max amount of strings that can be in the string pool's freelist.
	constexpr static auto kMaxStringPoolSize = 8;

	/// A pooled string.
	struct PooledString {
		PooledString() = default;
		PooledString(const PooledString&) = delete;
		PooledString(PooledString&&) = delete;

		std::string_view GetView() {
			return std::string_view(GetPointer(), length);
		}

		char* GetPointer() {
			// String memory is allocated after this header, so we can just use this.
			return reinterpret_cast<char*>(this + 1);
		}

		std::size_t GetLength() const {
			return length;
		}

	protected:
		friend StringPool;
		PooledString* freeListNext;
		std::size_t length;
	};

	StringPool() = default;
	StringPool(const StringPool&) = delete;
	StringPool(StringPool&&) = delete;

	~StringPool() {
		Clear();
	}

	/// Either gets a string from the string pool with a suitable capacity,
	/// or if no string was found with a suitable capacity, allocates one.
	///
	/// May return nullptr if allocating a string (if this function had to allocate) fails.
	/// 
	/// The return value of this function MUST be provided to [StringPool::ReturnString] when 
	/// the string is no longer in active use.
	PooledString* GetString(std::size_t wantedCapacity) {
		PooledString* pIter = freeListHead;
		while(pIter) {
			// We were able to find a pooled string which
			// has a suitable capacity. Simply return that string
			if(pIter->length >= wantedCapacity) {
				STRINGPOOL_DEBUG_PRINTF("Found string in freelist with suitable capacity %lu!!!\n", pIter->capacity);
				return pIter;
			}
			pIter = pIter->freeListNext;
		}

		STRINGPOOL_DEBUG_PRINTF("Could not find string for capacity %lu, allocating\n", wantedCapacity);

		// Give up and allocate a new string not on the freelist.
		return AllocateString(wantedCapacity);
	}

	/// "Returns" a string from the string pool. If the string is not in the freelist,
	/// it is added to the freelist, otherwise nothing happens.
	void ReturnString(PooledString* pPooledString) {
		if(pPooledString == nullptr)
			return;

		bool foundInList = false;

		if(freeListHead != nullptr) {
			PooledString* pFindIter = freeListHead;
			while(pFindIter) {
				if(pFindIter == pPooledString) {
					STRINGPOOL_DEBUG_PRINTF("Pooled string %p was found in freelist (%p)\n", pPooledString, pFindIter);
					foundInList = true;
					break;
				}

				pFindIter = pFindIter->freeListNext;
			}

			if(!foundInList) {
				STRINGPOOL_DEBUG_PRINTF("Did not find pooled string %p in list\n", pPooledString);
			}
		}

		// The string is already in the pool's freelist,
		// so we do not need to re-add it.
		if(foundInList)
			return;

		STRINGPOOL_DEBUG_PRINTF("Current freelist size: %lu\n", PoolSize());

		// Make sure the pool doesn't get so large that we start to be more of a memory leak.
		if(PoolSize() >= kMaxStringPoolSize) {
			STRINGPOOL_DEBUG_PRINTF("Clearing freelist, it is too large.\n");
			Clear();
		}

		if(freeListHead == nullptr) {
			STRINGPOOL_DEBUG_PRINTF("First freelist node\n");
			freeListHead = pPooledString;
		} else {
			STRINGPOOL_DEBUG_PRINTF("Not the first freelist node\n");

			// Walk the freelist for a node which has a null next pointer.
			// We will insert there.
			PooledString* pInsertIter = freeListHead;
			while(true) {
				if(pInsertIter->freeListNext == nullptr)
					break;

				STRINGPOOL_DEBUG_PRINTF("DEBUG: node %p, next %p\n", pInsertIter, pInsertIter->freeListNext);
				pInsertIter = pInsertIter->freeListNext;
			}

			pInsertIter->freeListNext = pPooledString;
		}
	}

	/// Clears the pool's freelist.
	void Clear() {
		PooledString* pIter = freeListHead;
		while(pIter) {
			// Need to store the possible next (or lack thereof)
			// since we are freeing the pooled string immediately.
			//
			// Bad, but it uses 16 bytes of stack space at the most,
			// compared to needing a list of pointers to free or something.
			auto next = pIter->freeListNext;
			FreeString(pIter);
			pIter = next;
		}	

		freeListHead = nullptr;
	}

	/// Returns the size of the pool's freelist.
	std::size_t PoolSize() {
		// It is empty.
		if(freeListHead == nullptr)
			return 0;

		PooledString* pInsertIter = freeListHead;
		std::size_t i = 0;
		while(pInsertIter) {
			i++;
			pInsertIter = pInsertIter->freeListNext;
		}

		return i;
	}

private:

	PooledString* AllocateString(std::size_t length) {
		auto pAlloced = calloc((length + 1 * sizeof(char)) + sizeof(PooledString), 1);
		if(pAlloced == nullptr)
			return nullptr;

		// The "hip" way of doing this is e.g: std::start_lifetime_as,
		// but placement new works just fine, and accomplishes the same goal.
		auto pString = new (pAlloced) PooledString;

		// Initialize the pooled string
		pString->length = length;
		pString->freeListNext = nullptr;
		//pString->pString = reinterpret_cast<char*>(pAlloced) + sizeof(PooledString);
		return pString;
	}

	void FreeString(PooledString* pPooled) {
		pPooled->~PooledString();
		free(pPooled);
	}

	PooledString* freeListHead;
};

StringPool TheStringPool;

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
		auto* pString = TheStringPool.GetString(formatLength);
		if(pString == nullptr)
			return;

		auto ptr = pString->GetPointer();

		va_start(val, format);
			// Format the string
			std::vsnprintf(ptr, formatLength, format, val);
		va_end(val);

		// Remove the last newline and replace it with a null terminator, since
		// Tracing will write a newline on its own.
		if(ptr[formatLength-1] == '\n')
			ptr[formatLength-1] = '\0';

		// Call the Rust-side reciever.
		letsplay_retro_frontend_log(level, ptr);

		// Return the string back to the pool, adding it to the list of 
		// now freed strings that we can re-use.
		TheStringPool.ReturnString(pString);
	}
}
