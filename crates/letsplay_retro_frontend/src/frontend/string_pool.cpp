
#include "string_pool.hpp"

#include <cstdlib>
#include <new>

// set to 1 to enable debug messages for the string pool
#define STRINGPOOL_DEBUG 0

#if STRINGPOOL_DEBUG
	#include <cstdio>
	#define STRINGPOOL_DPRINTF(fmt, ...) printf("stringpool debug: " fmt "\n", ##__VA_ARGS__)
#else
	#define STRINGPOOL_DPRINTF(fmt, ...)
#endif

namespace letsplay {

	namespace {

		/// The max amount of strings that can be in the string pool's freelist before
		/// we release all strings in it.
		constexpr static auto kMaxStringPoolSize = 8;

	} // namespace

	StringPool::~StringPool() {
		Clear();
	}

	StringPool::PooledString* StringPool::GetString(std::size_t wantedLength) {
		PooledString* pIter = freeListHead;
		while(pIter) {
			// We were able to find a pooled string which has a suitable capacity & is not in use.
			// Simply return that string. (Happy path)
			if(pIter->length >= wantedLength && !pIter->inUse) {
				STRINGPOOL_DPRINTF("Found unused string in freelist with suitable length for user request (user request %lu, actual length %lu)",
								   wantedLength, pIter->length);
				pIter->inUse = true;
				return pIter;
			}
			pIter = pIter->freeListNext;
		}

		STRINGPOOL_DPRINTF("Could not find string for user requested length %lu, allocating a new one", wantedLength);

		// Give up and allocate a new string not on the freelist. (sad path)
		return AllocateString(wantedLength);
	}

	void StringPool::ReturnString(PooledString* pPooledString) {
		if(pPooledString == nullptr)
			return;

		bool foundInList = false;

		if(freeListHead != nullptr) {
			PooledString* pFindIter = freeListHead;
			while(pFindIter) {
				if(pFindIter == pPooledString) {
					STRINGPOOL_DPRINTF("Pooled string %p was found in freelist (%p)", pPooledString, pFindIter);
					foundInList = true;
					break;
				}

				pFindIter = pFindIter->freeListNext;
			}

			if(!foundInList) {
				STRINGPOOL_DPRINTF("Did not find pooled string %p in list", pPooledString);
			}
		}

		// Mark the string as free.
		if(pPooledString->inUse)
			pPooledString->inUse = false;

		// The string is already in the pool's freelist,
		// so we do not need to re-add it.
		if(foundInList)
			return;

		STRINGPOOL_DPRINTF("Current freelist size: %lu", PoolSize());

		// Make sure the pool doesn't get so large that we start to be more of a memory leak.
		// FIXME: This doesn't consider strings which are in use. To make this fully safe for re-entrant usage
		// we probably should like.. do that.
		if(PoolSize() >= kMaxStringPoolSize) {
			STRINGPOOL_DPRINTF("Clearing freelist, it is too large.");
			Clear();
		}

		if(freeListHead == nullptr) {
			STRINGPOOL_DPRINTF("First freelist node");
			freeListHead = pPooledString;
		} else {
			STRINGPOOL_DPRINTF("Not the first freelist node");

			// Walk the freelist for a node which has a null next pointer.
			// We will insert there.
			PooledString* pInsertIter = freeListHead;
			while(true) {
				if(pInsertIter->freeListNext == nullptr)
					break;

				STRINGPOOL_DPRINTF("DEBUG: node %p, next %p", pInsertIter, pInsertIter->freeListNext);
				pInsertIter = pInsertIter->freeListNext;
			}

			pInsertIter->freeListNext = pPooledString;
		}
	}

	void StringPool::Clear() {
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

	std::size_t StringPool::PoolSize() {
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

	/*static*/ StringPool::PooledString* StringPool::AllocateString(std::size_t length) {
		// A string is allocated with the header (the PooledString structure) at the start,
		// then the string data (plus an additional character for the ~~C Mistake~~ NUL terminator)
		auto pAlloced = calloc((length + 1 * sizeof(char)) + sizeof(PooledString), 1);
		if(pAlloced == nullptr)
			return nullptr;

		// The "hip" way of doing this is e.g: std::start_lifetime_as,
		// but placement new works just fine, and accomplishes the same goal.
		auto pString = new(pAlloced) PooledString;

		// Initialize the pooled string structure.
		pString->length = length;
		pString->freeListNext = nullptr;
		pString->inUse = false;
		return pString;
	}

	/*static*/ void StringPool::FreeString(PooledString* pPooled) {
		pPooled->~PooledString();
		free(pPooled);
	}

} // namespace letsplay
