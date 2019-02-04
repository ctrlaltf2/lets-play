#pragma once
#ifdef USE_BOOST_FILESYSTEM
    #include "boost/filesystem.hpp"
#else
    #include <filesystem>
#endif

namespace lib {

#ifdef USE_BOOST_FILESYSTEM
    namespace filesystem = boost::filesystem;
#else
    namespace filesystem = std::filesystem;
#endif

}
