
// #include "avox/JsonValue.hpp"
#include <iostream>

#include "avox/module/Json.hpp"
using namespace avox;

void test1() {
  Json x1 = 1;
  int j1 = x1;
  Json x2 = "hello";
  std::string j2 = x2;
  Json x3 = true;
  Json x4;
  x4["name"] = 3.131;
  x4["name1"] = "hello";
  x4["name2"] = 3;
  x4["name4"] = true;
  std::string x5 = "name2";
  Json& x6 = x4[x5];
  x6 = 6;
  int x7 = x6;
  std::string x8 = x4.dump();
  std::cout << x8 << std::endl;
}

void test2() {
  // create an empty structure (null)
  Json j;

  // add a number that is stored as double (note the implicit conversion of j to
  // an object)
  j["pi"] = 3.141;

  // add a Boolean that is stored as bool
  j["happy"] = true;

  // add a string that is stored as std::string
  j["name"] = "Niels";
  std::string ij = j["name"];
  std::cout << ij << std::endl;
  // add another null object by passing nullptr
  j["nothing"] = nullptr;

  // add an object inside the object
  j["answer"]["everything"] = 42;

  // add an array that is stored as std::vector (using an initializer list)
  j["list"] = {1, 0, 2};

  j["object"] = {"currency", 1};
  std::string x = j["object"][0];
  std::cout << x << std::endl;
  j["object1"] = {"currency1", 11};
  // add another null object (using an initializer list of pairs)
  j["object4"] = {j["object"], j["object1"]};

  j["object2"] = {{"currency", 1}, {"value", 42.99}};
  // add another object (using an initializer list of pairs)
  j["object3"] = {{"currency", "USD"}, {"value", 42.99}};
}

void test3() {
  // instead, you could also write (which looks very similar to the JSON above)
  Json j2 = {{"pi", 3.141},
             {"happy", true},
             {"name", "Niels"},
             {"nothing", nullptr},
             {"answer", {{"everything", 42}}},
             {"list", {1, 0, 2}},
             {"object", {{"currency", "USD"}, {"value", 42.99}}}};
  std::cout << j2.dump() << std::endl;
}

Json parse() { return Json(11); }

void test4() {
  std::string json =
      "{\"name\": \"Alice\", \"age\": 20, \"isStudent\": true, \"grades\": "
      "[80, 90, 95], \"address\": {\"city\": \"Beijing\", \"country\": "
      "\"China\"}}";
  // std::string json = "{\"name\": \"Alice\"}";
  // 现在解析会有问题，还在分析原因
  // std::string json = "{\"xxxxxxa\": 20}";
  Json value = parserJson(json.c_str());
  std::cout << value.dump() << std::endl;

  // Json xobj = Json();
  // {
  //   std::string x = "xxxxxxa";
  //   xobj[x] = parse();
  // }
  // Json xobj1 = Json();
  // xobj1["xa"] = xobj;
  // std::cout << xobj1.dump() << std::endl;

  // value["age"] = 30;
  // std::map<std::string, JsonValue> object = value;
  std::cout << "name: " << value["name"].dump() << std::endl;
  std::cout << "age: " << value["age"].dump() << std::endl;
  std::cout << "isStudent: " << value["isStudent"].dump() << std::endl;

  Json& grades = value["grades"];
  grades[0] = 85;
  std::cout << "grades: [";
  for (size_t i = 0; i < grades.size(); ++i) {
    std::cout << (int64_t)grades[i];
    if (i != grades.size() - 1) {
      std::cout << ", ";
    }
  }
  std::cout << "]" << std::endl;

  auto& address = value["address"];
  address["city"] = "Shanghai";
  std::cout << "address: {city: " << value["address"]["city"].dump()
            << ", country: " << value["address"]["country"].dump() << "}"
            << std::endl;
}

void test5() {
  std::unique_ptr<IOption> j = std::unique_ptr<IOption>(createJsonOption());
  j->setString("name", "Alice");
  j->setInt("age", (int64_t)20);

  int32_t age = j->getInt("age");
  std::cout << "age: " << age << std::endl;
  std::string name = j->getString("name");
  std::cout << "name: " << name << std::endl;
}

int main() {
  test1();
  test2();
  test3();
  test5();
  test4();
  return 0;
}
