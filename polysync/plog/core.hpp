#pragma once

// Certain message types are defined at compile time, mostly from PolySync
// core.idl, here in core.hpp.  Ideally, this file would be generated by pdmgen
// to keep perfectly in sync with PolySync.  However, to keep general data
// science applications lightweight and standalone from the collection
// software, they are actually just copied here and will have to be manually
// updated as the plog format evolves.
//
// Parsing of the plog (reader.hpp) is typesafe and generic, even for the
// compile time defined message types found here.  This is implemented via
// boost::hana.  The hana wrappers are defined at the bottom of this file, and
// the reader class knows how to introspect these hana structures for
// serialization.
//
// Most of the PolySync message types are dynamically defined elsewhere, which
// is why this file is deliberately small.

#include <cstdint>
#include <vector>
#include <map>
#include <algorithm>

#include <boost/hana.hpp>

// Pull in a multiprecision type to support the PolySync hash and any other CRC types.
#include <boost/multiprecision/cpp_int.hpp>

namespace polysync { namespace plog {

constexpr size_t PSYNC_MODULE_VERIFY_HASH_LEN = 16;

namespace multiprecision = boost::multiprecision;

using hash_type = multiprecision::number<multiprecision::cpp_int_backend<
    PSYNC_MODULE_VERIFY_HASH_LEN*8, 
    PSYNC_MODULE_VERIFY_HASH_LEN*8, 
    multiprecision::unsigned_magnitude>>;

// a sequence<LenType, T> is just a vector<T> that knows to read it's length as a LenType
template <typename LenType, typename T>
struct sequence : std::vector<T> {
    using length_type = LenType;
};

template <typename LenType, typename T>
bool operator==(const sequence<LenType, T>& lhs, const sequence<LenType, T>& rhs) {
    if (lhs.size() != rhs.size())
        return false;
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), boost::hana::equal);
}

// specialize the std::uint8_t sequences because they are actually strings
template <typename LenType>
struct sequence<LenType, std::uint8_t> : std::string {
    using std::string::string;
    using length_type = LenType;
};

using name_type = sequence<std::uint16_t, std::uint8_t>;
using msg_type = std::uint32_t;
using guid = std::uint64_t;
using timestamp = std::uint64_t;

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
    msg_type type;
    plog::timestamp timestamp;
    guid src_guid;
};

struct log_record {
    std::uint32_t index;
    std::uint32_t size;
    std::uint32_t prev_size;
    plog::timestamp timestamp;
    std::string blob;
};

extern std::map<plog::msg_type, std::string> type_support_map;

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
BOOST_HANA_ADAPT_STRUCT(polysync::plog::log_record, index, size, prev_size, timestamp);
BOOST_HANA_ADAPT_STRUCT(polysync::plog::msg_header, type, timestamp, src_guid);


