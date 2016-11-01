#pragma once

#include <polysync/plog/tree.hpp>
#include <polysync/plog/description.hpp>
#include <polysync/plog/detector.hpp>

namespace endian = boost::endian;
namespace hana = boost::hana;

constexpr auto integers = hana::tuple_t<
    std::int8_t, std::int16_t, std::int32_t, std::int64_t,
    std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t
    >;

constexpr auto reals = hana::tuple_t<float, double>;

const auto bigendians = hana::transform(integers, [](auto t) { 
        using T = typename decltype(t)::type;
        return hana::type_c<endian::endian_arithmetic<endian::order::big, T, 8*sizeof(T)>>; 
        });

auto scalars = hana::concat(hana::concat(integers, bigendians), reals);

// Define metafunction to compute parameterized mettle::suite from a hana typelist.
template <typename TypeList>
using type_suite = typename decltype(
        hana::unpack(std::declval<TypeList>(), hana::template_<mettle::suite>)
        )::type;

// Teach mettle how to compare hana structures
template <typename Struct>
auto hana_equal(Struct&& expected) {

    return mettle::make_matcher(std::forward<Struct>(expected), 
            [](const auto& actual, const auto& expected) -> bool { 
            return hana::equal(actual, expected); },
            "hana_equal ");
}

namespace polysync { namespace plog { namespace descriptor { 

template <typename Struct>
auto operator==(const Struct& lhs, const Struct& rhs) -> bool { 
    return hana::equal(lhs, rhs); 
}

}}}

namespace cpptoml {

// Teach mettle how to print TOML objects.  Must be in cpptoml namespace for
// discovery by argument dependent lookup.
template <typename T>
std::string to_printable(std::shared_ptr<T> p) { 
    std::ostringstream stream;
    cpptoml::toml_writer writer { stream };
    p->accept(writer);
    return stream.str(); 
}

} // namespace cpptoml

struct has_key : mettle::matcher_tag {
    has_key(const std::string& key) : key(key) {}

    template <typename T, typename V>
    bool operator()(const std::map<T, V>& map) const {
        return map.count(key);
    }
    
    bool operator()(std::shared_ptr<cpptoml::base> base) const {
           if (!base->is_table())
               return false; 
            return base->as_table()->contains(key);
    };    
    
    std::string desc() const { return "has_key \"" + key + "\""; }
    const std::string key;
};

