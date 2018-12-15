#include "Random.h"

namespace rnd {
    std::uint_fast32_t nextInt() {
        static thread_local std::mt19937 gen(std::chrono::system_clock::now().time_since_epoch().count());
        return gen();
    }
}
