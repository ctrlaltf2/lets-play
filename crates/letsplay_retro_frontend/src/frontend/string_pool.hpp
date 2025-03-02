#pragma once

#include <cstddef>
#include <string_view>

namespace letsplay {

	/// A very simple string pool implemented using a doubly linked list as a freelist.
	struct StringPool {
		/// A pooled string.
		struct PooledString {
			PooledString() = default;
			PooledString(const PooledString&) = delete;
			PooledString(PooledString&&) = delete;

			inline std::string_view GetView() { return std::string_view(GetPointer(), length); }

			inline char* GetPointer() {
				// String memory is allocated after this header, so we can just use this.
				return reinterpret_cast<char*>(this + 1);
			}

			constexpr std::size_t GetLength() const { return length; }

		   protected:
			friend StringPool;

			PooledString* freeListPrev;
			PooledString* freeListNext;

			std::size_t length;
			bool inUse;
		};

		StringPool() = default;
		StringPool(const StringPool&) = delete;
		StringPool(StringPool&&) = delete;

		~StringPool();

		/// Either gets a string from the string pool with a suitable capacity,
		/// or if no string was found with a suitable capacity that is not in use, allocates one.
		///
		/// This function is also permitted to allocate a string if it cannot find
		/// a string that's not in use in the freelist, even if it would have found
		/// a string with a suitable capacity.
		///
		/// May return nullptr if allocating a string (if this function had to allocate) failed.
		///
		/// The return value of this function MUST be provided to [StringPool::ReturnString] when
		/// the string is no longer in active use.
		///
		PooledString* GetString(std::size_t wantedLength);

		/// "Returns" a string from the string pool. If the string is not in the freelist,
		/// it is added to the freelist, otherwise nothing happens. Additionally, if the
		/// freelist is considered too large, this function will automatically perform 
		/// garbage collection/compaction of strings which are not in use.
		void ReturnString(PooledString* pPooledString);

		/// Clears the pool's freelist.
		void Clear();

		/// Collects "garbage" unused strings. Only strings which
		/// are currently in use (and thus should not be removed/freed)
		/// will be kept in the pool's freelist.
		void GarbageCollect();

	   private:
		/// Returns the size of the pool's freelist.
		std::size_t PoolSize();

		PooledString* RemoveString(PooledString* str);

		static PooledString* AllocateString(std::size_t length);
		static void FreeString(PooledString* pPooled);

		PooledString* freeListHead;
		PooledString* freeListTail;
	};

} // namespace letsplay
