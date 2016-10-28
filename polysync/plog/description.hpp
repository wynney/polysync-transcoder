#pragma once

// Most PolySync and vendor specific message types are defined dynamically,
// using TOML strings embedded in the plog files, or external config files.
// Legacy types will be supported by a fallback in external files.  Ubiquitous
// message types are found in every plog file, and are specially defined in
// core.hpp, and not by the dynamic mechanism implemented here.

#include <polysync/plog/tree.hpp>
#include <polysync/3rdparty/cpptoml.h>

#include <boost/hana.hpp>
#include <typeindex>

namespace polysync { namespace plog {

namespace descriptor {

struct field {
    std::string name;
    std::string type;
};

// The full type description is just a vector of fields.  This has to be a
// vector, not a map, to preserve the serialization order in the plog flat file.
using type = std::vector<field>;

struct atom {
    std::string name;
    std::streamoff size;
};

using catalog_type = std::map<std::string, type>;

// Traverse a TOML table 
extern void load(const std::string& name, std::shared_ptr<cpptoml::table> table, catalog_type&);

// Global type descriptor catalogs
extern catalog_type catalog;
extern std::map<std::type_index, atom> static_typemap; 
extern std::map<std::string, atom> dynamic_typemap; 

// Create a type description of a static structure, using hana for class instrospection
template <typename Struct>
inline type describe() {
    namespace hana = boost::hana;

    return hana::fold(Struct(), type(), [](auto desc, auto pair) { 
            std::string name = hana::to<char const*>(hana::first(pair));
            if (static_typemap.count(typeid(hana::second(pair))) == 0)
                throw std::runtime_error("missing typemap for " + name);
            atom a = static_typemap.at(typeid(hana::second(pair)));
            desc.emplace_back(field { name, a.name });
            return desc;
            });
}

} // namespace descriptor

// Define some metaprograms to compute the sizes of types.
template <typename Number, class Enable = void>
struct size {
    static std::streamoff value() { return sizeof(Number); }
};

template <typename Struct>
struct size<Struct, typename std::enable_if<hana::Foldable<Struct>::value>::type> {
    static std::streamoff value() {
        return hana::fold(hana::members(Struct()), 0, [](std::streamoff s, auto field) { 
                return s + size<decltype(field)>::value(); 
                });
    }
};


template <>
struct size<descriptor::field> {
    size(const descriptor::field& f) : desc(f) { }
    
    std::streamoff value() const {
        return descriptor::dynamic_typemap.at(desc.type).size;
    }

    descriptor::field desc;
};

}} // namespace polysync::plog

BOOST_HANA_ADAPT_STRUCT(polysync::plog::descriptor::field, name, type);


