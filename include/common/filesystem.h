#ifdef USE_BOOST_FILESYSTEM
#include "boost/filesystem.hpp"
namespace lib::filesystem = boost;
#else
#include <filesystem>
namespace lib::filesystem = std::filesystem;
#endif
