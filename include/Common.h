/**
 * @file Common.h
 *
 * @author ctrlaltf2
 *
 * @section LICENSE
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  @section DESCRIPTION
 *  Common typedefs used in multiple files.
 */

#include <memory>
#include <string>

#include "LetsPlayUser.h"

/**
 * LetsPlayUser objects are stored in LetsPlayServer::m_Users as std::shared_ptrs. std::weak_ptr
 * references are passed around and when access to the user object is needed in other threads,
 * std::weak_ptr::lock() is called. If expired, the action that would have taken place with the
 * user object is safely skipped.
 */
using LetsPlayUserHdl = std::weak_ptr<LetsPlayUser>;

/**
 * The type used to identify emulators.
 */
using EmuID_t = std::string;