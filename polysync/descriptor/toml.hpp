#pragma once

#include <deps/cpptoml.h>

#include <polysync/descriptor/catalog.hpp>

namespace polysync { namespace descriptor {

extern std::vector<Type> loadCatalog( const std::string&, std::shared_ptr<cpptoml::table> );

}} // namespace polysync::descriptor
