#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include <utility>
#include <stdexcept>
#include <type_traits>
#include "ddb/index/bplus_tree.h"
namespace ddb::execution {
enum class ValueType : std::uint8_t { Int64, Double, Boolean, String };
using Value = std::variant<std::int64_t,double,bool,std::string>;
struct Column { std::string name; ValueType type; friend bool operator==(const Column&,const Column&)=default; };
class Schema { public: explicit Schema(std::vector<Column> columns):columns_(std::move(columns)){} [[nodiscard]] const std::vector<Column>& columns()const{return columns_;} [[nodiscard]] std::size_t size()const{return columns_.size();} [[nodiscard]] bool validates(const std::vector<Value>& v)const {if(v.size()!=columns_.size())return false;for(std::size_t i=0;i<v.size();++i)if(static_cast<ValueType>(v[i].index())!=columns_[i].type)return false;return true;} private:std::vector<Column> columns_;};
struct Tuple { std::vector<Value> values; friend bool operator==(const Tuple&,const Tuple&)=default; };
using RecordId=index::RecordId;
[[nodiscard]] inline int compare_values(const Value&a,const Value&b){if(a.index()!=b.index())throw std::invalid_argument("cannot compare unlike values");return std::visit([](const auto&x,const auto&y)->int{using X=std::decay_t<decltype(x)>;using Y=std::decay_t<decltype(y)>;if constexpr(!std::is_same_v<X,Y>)return 0;else return x<y?-1:(x>y?1:0);},a,b);}
}
