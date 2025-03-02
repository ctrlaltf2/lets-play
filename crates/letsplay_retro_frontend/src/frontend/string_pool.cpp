
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

		/// The max amount of strings in reserve that can be in the string pool's freelist before
		/// we perform garbage colllection to free some memory.
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
			STRINGPOOL_DPRINTF("Garbage collecting freelist, it is too large.");
			GarbageCollect();
		}

		// Insert into the free list.
		pPooledString->freeListPrev = freeListTail;
		if(freeListTail)
			freeListTail->freeListNext = pPooledString;
		else
			freeListHead = pPooledString;

		freeListTail = pPooledString;
	}

	void StringPool::Clear() {
		// Clear all strings.
		while(freeListHead) {
			if(auto* ptr = RemoveString(freeListTail); ptr != nullptr) {
				STRINGPOOL_DPRINTF("Clear(): Freeing removed string %p", ptr);
				FreeString(ptr);
			}
		}
	}

	void StringPool::GarbageCollect() {
		if(!freeListHead)
			return;

		PooledString* pIter = freeListTail;

		while(pIter) {
			auto next = pIter->freeListPrev;

			// String is in use, so move on to the next possible string.
			if(pIter->inUse) {
				pIter = next;
				continue;
			} else {
				STRINGPOOL_DPRINTF("Found unused string with length %lu we can remove from freelist: %p", pIter->length, pIter);
				// Remove string from the list.
				if(auto* ptr = RemoveString(pIter); ptr != nullptr)
					FreeString(ptr);
				pIter = next;
			}
		}
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

	StringPool::PooledString* StringPool::RemoveString(PooledString* str) {
		if(!str)
			return nullptr;

		if(str->freeListPrev != nullptr)
			str->freeListPrev->freeListNext = str->freeListNext;
		if(str->freeListNext != nullptr)
			str->freeListNext->freeListPrev = str->freeListPrev;

		if(str == freeListHead)
			freeListHead = str->freeListNext;

		if(str == freeListTail)
			freeListTail = str->freeListPrev;

		str->freeListNext = nullptr;
		str->freeListPrev = nullptr;
		return str;
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
