#pragma once

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/hana.hpp>
#include <eggs/variant.hpp>
#include <vector>
#include <cstdint>
#include <typeindex>
#include <map>

namespace polysync { namespace plog {

struct log_module;
struct type_support;

constexpr size_t PSYNC_MODULE_VERIFY_HASH_LEN = 16;

namespace multiprecision = boost::multiprecision;
namespace hana = boost::hana;

using hash_type = multiprecision::number<multiprecision::cpp_int_backend<
    PSYNC_MODULE_VERIFY_HASH_LEN*8, 
    PSYNC_MODULE_VERIFY_HASH_LEN*8, 
    multiprecision::unsigned_magnitude>>;

// a sequence<LenType, T> is just a vector<T> that knows to read it's length as a LenType
template <typename LenType, typename T>
struct sequence : std::vector<T> { };

// specialize the std::uint8_t sequences because they are actually strings
template <typename LenType>
struct sequence<LenType, std::uint8_t> : std::string {};

using name_type = sequence<std::uint16_t, std::uint8_t>;
using ps_msg_type = std::uint32_t;
using ps_guid = std::uint64_t;

struct log_module {
    std::uint8_t version_major;
    std::uint8_t version_minor;
    std::uint16_t version_subminor;
    std::uint32_t build_date;
    hash_type build_hash;
    name_type name;
};

struct type_support {
    std::uint32_t type;
    name_type name;
};
       
struct log_header {
    std::uint8_t version_major;
    std::uint8_t version_minor;
    std::uint16_t version_subminor;
    std::uint32_t build_date;
    std::uint64_t node_guid;
    sequence<std::uint32_t, log_module> modules;
    sequence<std::uint32_t, type_support> type_supports;
};

struct msg_header {
    ps_msg_type type;
    std::uint64_t timestamp;
    ps_guid src_guid;
};

// I put msg_header into log_header instead of the byte_array like PolySync
// core does.  This is because we must always branch on msg_header.type before
// the byte_array can be interpreted.  So this is a difference between
// transcode and PolySync core.
struct log_record {
    std::uint32_t index;
    std::uint32_t size;
    std::uint32_t prev_size;
    std::uint64_t timestamp;
    msg_header header;
};

struct field_descriptor {
    std::string name;
    std::string type;
};

struct type_descriptor {
    name_type name;
    sequence<std::uint32_t, field_descriptor> desc;
};

template <typename Number, class Enable = void>
struct size {
    static std::streamoff packed() { return sizeof(Number); }
};

template <typename Struct>
struct size<Struct, typename std::enable_if<hana::Foldable<Struct>::value>::type> {
    static std::streamoff packed() {
        return hana::fold(hana::members(Struct()), 0, [](std::streamoff s, auto field) { 
                return s + size<decltype(field)>::packed(); 
                });
    }
};

using record_type = log_record;

struct atom_description {
    std::string name;
    std::streamoff size;
};

extern std::map<std::type_index, atom_description> static_typemap; 
extern std::map<std::string, atom_description> dynamic_typemap; 

template <typename Struct>
inline std::vector<field_descriptor> describe() {
    return hana::fold(Struct(), std::vector<field_descriptor>(), [](auto desc, auto pair) { 
            std::string name = hana::to<char const*>(hana::first(pair));
            if (static_typemap.count(typeid(hana::second(pair))) == 0)
                throw std::runtime_error("missing typemap for " + name);
            plog::atom_description atom = static_typemap.at(typeid(hana::second(pair)));
            desc.emplace_back(field_descriptor { name, atom.name });
            return desc;
            });
}


}} // namespace polysync::plog

// Hana adaptors must be in global namespace

BOOST_HANA_ADAPT_STRUCT(polysync::plog::log_header,
        version_major, version_minor, version_subminor, build_date, node_guid 
       , modules, type_supports
        );
BOOST_HANA_ADAPT_STRUCT(polysync::plog::log_module, version_major, version_minor, version_subminor, build_date
        , build_hash, name
         );
BOOST_HANA_ADAPT_STRUCT(polysync::plog::type_support, type, name);
BOOST_HANA_ADAPT_STRUCT(polysync::plog::log_record, index, size, prev_size, timestamp, header);
BOOST_HANA_ADAPT_STRUCT(polysync::plog::msg_header, type, timestamp, src_guid);


