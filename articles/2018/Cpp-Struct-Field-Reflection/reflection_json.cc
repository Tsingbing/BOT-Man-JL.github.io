// clang++ reflection_json.cc -std=c++14 -Wall -o ref_json && ./ref_json

#include <iostream>
#include <string>
#include <vector>

#include "static_reflection.h"
#include "third_party/optional_json.h"

namespace {

template <typename T>
struct is_optional : std::false_type {};

template <typename T>
struct is_optional<std::unique_ptr<T>> : std::true_type {};

template <typename T>
constexpr bool is_optional_v = is_optional<std::decay_t<T>>::value;

template <typename T>
constexpr bool has_schema = std::tuple_size<decltype(StructSchema<T>())>::value;

}  // namespace

namespace nlohmann {

template <typename T>
struct adl_serializer<T, std::enable_if_t<::has_schema<T>>> {
  template <typename BasicJsonType>
  static void to_json(BasicJsonType& j, const T& value) {
    ForEachField(value, [&j](auto&& field, auto&& name) { j[name] = field; });
  }

  template <typename BasicJsonType>
  static void from_json(const BasicJsonType& j, T& value) {
    ForEachField(value, [&j](auto&& field, auto&& name) {
      // ignore missing field of optional
      if (::is_optional_v<decltype(field)> && j.find(name) == j.end())
        return;

      j.at(name).get_to(field);
    });
  }
};

}  // namespace nlohmann

struct SimpleStruct {
  bool bool_;
  int int_;
  double double_;
  std::string string_;
  std::vector<double> vector_;
  std::unique_ptr<bool> optional_;
};

DEFINE_STRUCT_SCHEMA(SimpleStruct,
                     DEFINE_STRUCT_FIELD(bool_, "_bool"),
                     DEFINE_STRUCT_FIELD(int_, "_int"),
                     DEFINE_STRUCT_FIELD(double_, "_double"),
                     DEFINE_STRUCT_FIELD(string_, "_string"),
                     DEFINE_STRUCT_FIELD(vector_, "_vector"),
                     DEFINE_STRUCT_FIELD(optional_, "_optional"));

int main() {
  using nlohmann::json;
  std::cout << json(json::parse("{"
                                "  \"_bool\": true,"
                                "  \"_int\": 2,"
                                "  \"_double\": 2.0,"
                                "  \"_string\": \"hello reflection json\","
                                "  \"_vector\": [2, 2.0]"
                                "}")
                        .get<SimpleStruct>())
            << std::endl
            << json(json::parse("{"
                                "  \"_bool\": true,"
                                "  \"_int\": 2,"
                                "  \"_double\": 2.0,"
                                "  \"_string\": \"hello reflection json\","
                                "  \"_vector\": [2, 2.0],"
                                "  \"_optional\": true"
                                "}")
                        .get<SimpleStruct>())
            << std::endl;
  return 0;
}
