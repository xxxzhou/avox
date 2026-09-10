// avox::Json 单元测试
// 从 samples/functest/jsontest.cpp 迁来: 原文件只是把结果打印出来, 没有任何判定,
// 这里把打印换成断言 (构造/取值/嵌套/解析 四条主线)。
#include <doctest.h>

#include <string>

#include "avox/module/Json.hpp"

namespace avox {

TEST_CASE("Json: 标量构造与隐式取值") {
  Json i = 1;
  int vi = i;
  CHECK(vi == 1);

  Json i64 = (int64_t)7;
  int64_t v64 = i64;
  CHECK(v64 == 7);

  Json s = "hello";
  std::string vs = s;
  CHECK(vs == "hello");

  Json f = 3.131;
  CHECK(!f.dump().empty());
}

TEST_CASE("Json: 对象字段读写, 取出的引用可回写") {
  Json o;
  o["name"] = 3.131;
  o["name1"] = "hello";
  o["name2"] = 3;

  std::string key = "name2";
  Json& ref = o[key];  // 非常量 key 走同一份数据, 回写要能生效
  ref = 6;
  int v = ref;
  CHECK(v == 6);

  std::string dump = o.dump();
  CHECK(dump.find("hello") != std::string::npos);
  CHECK(dump.find("name2") != std::string::npos);
}

TEST_CASE("Json: 数组与嵌套下标") {
  Json j;
  j["list"] = {1, 0, 2};
  CHECK(j["list"].size() == 3);
  int first = j["list"][0];
  CHECK(first == 1);

  j["object"] = {"currency", 1};
  std::string cur = j["object"][0];
  CHECK(cur == "currency");
}

TEST_CASE("Json: parserJson 解析对象/数组/嵌套") {
  const char* text =
      "{\"name\": \"Alice\", \"age\": 20, \"isStudent\": true, \"grades\": "
      "[80, 90, 95], \"address\": {\"city\": \"Beijing\", \"country\": "
      "\"China\"}}";
  Json v = parserJson(text);

  std::string name = v["name"];
  CHECK(name == "Alice");
  int age = v["age"];
  CHECK(age == 20);
  CHECK(v["grades"].size() == 3);
  std::string city = v["address"]["city"];
  CHECK(city == "Beijing");
}

}  // namespace avox
