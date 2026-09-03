#pragma once

#include <algorithm>
#include <cassert>
#include <ciso646>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <iterator>
#include <vector>

#include "../AvoxBase.h"

namespace avox {

template <typename T>
struct has_mapped_type {
 private:
  template <typename C>
  static char test(typename C::mapped_type*);
  template <typename C>
  static char (&test(...))[2];

 public:
  static constexpr bool value = sizeof(test<T>(0)) == 1;
};
// 参考 https://github.com/nlohmann/json/blob/develop/include/nlohmann/json.hpp
// 简化实现
class Json final: public IOption {
 public:
  using ValueType = Json;
  using Ref = ValueType&;
  using ConstRef = const ValueType&;
  // Json内存分配
  using Allocator = std::allocator<Json>;
  // allocator_traits标准化内存分配，构造，指针获取
  using Ptr = typename std::allocator_traits<Allocator>::pointer;
  using ConstPtr = typename std::allocator_traits<Allocator>::const_pointer;

  using JsonObject =
      std::map<std::string, Json, std::less<std::string>,
               std::allocator<std::pair<const std::string, Json>>>;
  using JsonArray = std::vector<Json, std::allocator<Json>>;
  using JsonStr = std::string;
  using JsonBool = bool;
  using JsonInt = int64_t;
  using JsonFloat = double;
  using size_type = std::size_t;

 private:
  // 帮助安全申请对象
  template <typename T, typename... Args>
  static T* create(Args&&... args) {
    std::allocator<T> alloc;
    // unique_ptr释放时调用
   auto deleter = [&](T* object) { alloc.deallocate(object, 1); };
    std::unique_ptr<T, decltype(deleter)> object(alloc.allocate(1), deleter);
    // 使用placement new替代alloc.construct
    new (object.get()) T(std::forward<Args>(args)...);
    return object.release();
  }

  union Value {
    /// object (stored with pointer to save storage)
    JsonObject* object;
    /// array (stored with pointer to save storage)
    JsonArray* array;
    /// string (stored with pointer to save storage)
    JsonStr* string;
    /// boolean
    JsonBool boolean;
    /// number (integer)
    JsonInt number_integer;
    /// number (floating-point)
    JsonFloat number_float;

    /// default constructor (for null values)
    Value() = default;
    /// constructor for booleans
    Value(JsonBool v) noexcept : boolean(v) {}
    /// constructor for numbers (integer)
    Value(JsonInt v) noexcept : number_integer(v) {}
    /// constructor for numbers (unsigned)
    Value(uint32_t v) noexcept : number_integer(v) {}
    Value(int32_t v) noexcept : number_integer(v) {}
    /// constructor for numbers (floating-point)
    Value(JsonFloat v) noexcept : number_float(v) {}
    Value(float v) noexcept : number_float(v) {}
    /// constructor for empty values of a given m_type
    Value(ArgType t) {
      switch (t) {
        case ArgType::Object: {
          object = create<JsonObject>();
          break;
        }

        case ArgType::Array: {
          array = create<JsonArray>();
          break;
        }

        case ArgType::String: {
          string = create<JsonStr>("");
          break;
        }

        case ArgType::Boolean: {
          boolean = JsonBool(false);
          break;
        }

        case ArgType::Int: {
          number_integer = JsonInt(0);
          break;
        }

        case ArgType::Number: {
          number_float = JsonFloat(0.0);
          break;
        }

        default: {
          break;
        }
      }
    }

    /// constructor for strings
    Value(const JsonStr& value) { string = create<JsonStr>(value); }
    /// constructor for objects
    Value(const JsonObject& value) { object = create<JsonObject>(value); }

    /// constructor for arrays
    Value(const JsonArray& value) { array = create<JsonArray>(value); }
  };
  ArgType m_type = ArgType::Null;
  // union类型
  Value m_value = {};

 public:
  // 运行时确定类型
  constexpr ArgType type() const noexcept { return m_type; }
  constexpr bool bNull() const noexcept { return m_type == ArgType::Null; }
  constexpr bool bInt() const noexcept { return m_type == ArgType::Int; }
  constexpr bool bNumber() const noexcept {
    return m_type == ArgType::Number;
  }
  constexpr bool bString() const noexcept {
    return m_type == ArgType::String;
  }
  constexpr bool bBool() const noexcept {
    return m_type == ArgType::Boolean;
  }
  constexpr bool bObject() const noexcept {
    return m_type == ArgType::Object;
  }
  constexpr bool bArray() const noexcept { return m_type == ArgType::Array; }

 public:
  Json(ArgType stype) : m_type(stype), m_value(stype) {}
  Json() = default;
  Json(std::nullptr_t) : Json(ArgType::Null) {}
  Json(const JsonObject& val) : m_type(ArgType::Object), m_value(val) {}
  Json(const JsonArray& val) : m_type(ArgType::Array), m_value(val) {}
  Json(const JsonStr& val) : m_type(ArgType::String), m_value(val) {}
  // const char*
  Json(const typename JsonStr::value_type* val) : Json(JsonStr(val)) {}
  //
  template <class T,
            typename std::enable_if<std::is_constructible<JsonStr, T>::value,
                                    int>::type = 0>
  Json(const T& val) : Json(JsonStr(val)) {}
  Json(JsonBool val) : m_type(ArgType::Boolean), m_value(val) {}
  Json(const JsonInt val) : m_type(ArgType::Int), m_value(val) {}
  Json(const int32_t val) : m_type(ArgType::Int), m_value(val) {}
  Json(const uint32_t val) : m_type(ArgType::Int), m_value(val) {}
  Json(const JsonFloat val) : m_type(ArgType::Number), m_value(val) {}
  Json(const float val) : m_type(ArgType::Number), m_value(val) {}
  Json(std::initializer_list<Json> init, bool type_deduction = true,
       ArgType manual_type = ArgType::Array) {
    // check if each element is an array with two elements whose first
    // element is a string
    bool is_an_object =
        std::all_of(init.begin(), init.end(), [](const Json& element) {
          return element.bArray() and element.size() == 2 and
                 element[0].bString();
        });

    // adjust type if type deduction is not wanted
    if (not type_deduction) {
      // if array is wanted, do not create an object though possible
      if (manual_type == ArgType::Array) {
        is_an_object = false;
      }

      // if object is wanted but impossible, throw an exception
      if (manual_type == ArgType::Object and not is_an_object) {
        std::domain_error("cannot create object from initializer list");
      }
    }

    if (is_an_object) {
      // the initializer list is a list of pairs -> create object
      m_type = ArgType::Object;
      m_value = ArgType::Object;
      std::for_each(init.begin(), init.end(), [this](const Json& element) {
        m_value.object->emplace(*(element[0].m_value.string), element[1]);
      });
    } else {
      // the initializer list describes an array -> create array
      m_type = ArgType::Array;
      m_value.array = create<JsonArray>(init);
    }
  }

  // 复制构造，深拷贝，不然类似{"name1": "zx",
  // "name2":2}构造完成后，析构时会把zx释放
  Json(const Json& other) : m_type(other.m_type) {
    switch (m_type) {
      case ArgType::Object: {
        assert(other.m_value.object != nullptr);
        m_value = *other.m_value.object;
        break;
      }

      case ArgType::Array: {
        assert(other.m_value.array != nullptr);
        m_value = *other.m_value.array;
        break;
      }

      case ArgType::String: {
        assert(other.m_value.string != nullptr);
        m_value = *other.m_value.string;
        break;
      }

      case ArgType::Boolean: {
        m_value = other.m_value.boolean;
        break;
      }

      case ArgType::Int: {
        m_value = other.m_value.number_integer;
        break;
      }

      case ArgType::Number: {
        m_value = other.m_value.number_float;
        break;
      }
      default: {
        break;
      }
    }
  }
  Json(Json&& other)
      : m_type(std::move(other.m_type)), m_value(std::move(other.m_value)) {
    other.m_type = ArgType::Null;
    other.m_value = {};
  }

  virtual ~Json() {
    switch (m_type) {
      case ArgType::Object: {
        std::allocator<JsonObject> alloc;
        // 直接调用析构函数替代alloc.destroy
        m_value.object->~JsonObject();
        // 释放内存
        alloc.deallocate(m_value.object, 1);
        break;
      }

      case ArgType::Array: {
        std::allocator<JsonArray> alloc;
        m_value.array->~JsonArray();
        alloc.deallocate(m_value.array, 1);
        break;
      }

      case ArgType::String: {
        std::allocator<JsonStr> alloc;
        m_value.string->~JsonStr();
        alloc.deallocate(m_value.string, 1);
        break;
      }

      default: {
        // all other types need no specific destructor
        break;
      }
    }
  }

 public:
  Ref& operator=(Json other) {
    using std::swap;
    swap(m_type, other.m_type);
    swap(m_value, other.m_value);
    return *this;
  }
  constexpr operator ArgType() const { return m_type; }
  // object/array
  constexpr bool bStruct() const { return bObject() || bArray(); }

  // IOption实现，json object用来做配置
 public:
  // 如果没有key，get直接异常，可以先用这判断
  virtual ArgType getType(const char* key) override {
    if (find(key)) {
      return at(key).type();
    }
    return ArgType::Null;
  };
  virtual void setBool(const char* key, bool value) override {
    operator[](key) = value;
  };
  virtual void setInt(const char* key, int64_t value) override {
    operator[](key) = value;
  };
  virtual void setString(const char* key, const char* value) override {
    operator[](key) = value;
  };
  virtual void setNumber(const char* key, double value) override {
    operator[](key) = value;
  };
  // 如果没有key，直接异常
  virtual int64_t getInt(const char* key) override { return at(key); };
  virtual double getDouble(const char* key) override { return at(key); };
  virtual const char* getString(const char* key) override {
    return at(key).m_value.string->c_str();
  };
  virtual bool getBool(const char* key) override { return at(key); };

 private:
#pragma region get_reference
  // 具化上面返回std::map
  JsonObject get_impl(JsonObject*) const {
    if (bObject()) {
      assert(m_value.object != nullptr);
      return *(m_value.object);
    } else {
      throw std::domain_error("type must be object, but is " + type_name());
    }
  }

  // 数组转化类数组T,要求T类型能转换为ValueType
  // 但是类型不能完全一样，也不能是numeric/string/T,这些后面有具化实现
  template <
      class T,
      typename std::enable_if<
          std::is_convertible<ValueType, typename T::value_type>::value and
              not std::is_same<ValueType, typename T::value_type>::value and
              not std::is_arithmetic<T>::value and
              not std::is_convertible<std::string, T>::value and
              not has_mapped_type<T>::value,
          int>::type = 0>
  T get_impl(T*) const {
    if (bArray()) {
      T to_vector;
      assert(m_value.array != nullptr);
      std::transform(m_value.array->begin(), m_value.array->end(),
                     std::inserter(to_vector, to_vector.end()),
                     [](Json i) { return i.get<typename T::value_type>(); });
      return to_vector;
    } else {
      throw std::domain_error("type must be array, but is " + type_name());
    }
  }

  // 具化上面的JsonArray，转容器里数据的类型，如std::vector<JsonInt>转化成std::vector<uint>
  template <class T,
            typename std::enable_if<std::is_convertible<ValueType, T>::value and
                                        not std::is_same<ValueType, T>::value,
                                    int>::type = 0>
  std::vector<T> get_impl(std::vector<T>*) const {
    if (bArray()) {
      std::vector<T> to_vector;
      assert(m_value.array != nullptr);
      to_vector.reserve(m_value.array->size());
      std::transform(m_value.array->begin(), m_value.array->end(),
                     std::inserter(to_vector, to_vector.end()),
                     [](Json i) { return i.get<T>(); });
      return to_vector;
    } else {
      throw std::domain_error("type must be array, but is " + type_name());
    }
  }

  // 具化上面的JsonArray，转容器，如std::vector<Json>转化成std::list<Json>
  template <class T, typename std::enable_if<
                         std::is_same<Json, typename T::value_type>::value and
                             not has_mapped_type<T>::value,
                         int>::type = 0>
  T get_impl(T*) const {
    if (bArray()) {
      assert(m_value.array != nullptr);
      return T(m_value.array->begin(), m_value.array->end());
    } else {
      throw std::domain_error("type must be array, but is " + type_name());
    }
  }
  // 具化上面的JsonArray，什么都不转
  JsonArray get_impl(JsonArray*) const {
    if (bArray()) {
      assert(m_value.array != nullptr);
      return *(m_value.array);
    } else {
      throw std::domain_error("type must be array, but is " + type_name());
    }
  }

  // 字符串
  template <typename T,
            typename std::enable_if<std::is_convertible<JsonStr, T>::value,
                                    int>::type = 0>
  T get_impl(T*) const {
    if (bString()) {
      assert(m_value.string != nullptr);
      return *m_value.string;
    } else {
      throw std::domain_error("type must be string, but is " + type_name());
    }
  }

  // 数值
  template <typename T, typename std::enable_if<std::is_arithmetic<T>::value,
                                                int>::type = 0>
  T get_impl(T*) const {
    switch (m_type) {
      case ArgType::Int: {
        return static_cast<T>(m_value.number_integer);
      }
      case ArgType::Number: {
        return static_cast<T>(m_value.number_float);
      }
      default: {
        throw std::domain_error("type must be number, but is " + type_name());
      }
    }
  }

  // 布尔值
  constexpr JsonBool get_impl(JsonBool*) const {
    return bBool() ? m_value.boolean
                   : throw std::domain_error("type must be boolean, but is " +
                                             type_name());
  }

  JsonObject* get_impl_ptr(JsonObject*) noexcept {
    return bObject() ? m_value.object : nullptr;
  }

  constexpr const JsonObject* get_impl_ptr(const JsonObject*) const noexcept {
    return bObject() ? m_value.object : nullptr;
  }

  JsonArray* get_impl_ptr(JsonArray*) noexcept {
    return bArray() ? m_value.array : nullptr;
  }

  constexpr const JsonArray* get_impl_ptr(const JsonArray*) const noexcept {
    return bArray() ? m_value.array : nullptr;
  }

  JsonStr* get_impl_ptr(JsonStr*) noexcept {
    return bString() ? m_value.string : nullptr;
  }

  constexpr const JsonStr* get_impl_ptr(const JsonStr*) const noexcept {
    return bString() ? m_value.string : nullptr;
  }

  JsonBool* get_impl_ptr(JsonBool*) noexcept {
    return bBool() ? &m_value.boolean : nullptr;
  }

  constexpr const JsonBool* get_impl_ptr(const JsonBool*) const noexcept {
    return bBool() ? &m_value.boolean : nullptr;
  }

  JsonInt* get_impl_ptr(JsonInt*) noexcept {
    return bInt() ? &m_value.number_integer : nullptr;
  }

  constexpr const JsonInt* get_impl_ptr(const JsonInt*) const noexcept {
    return bInt() ? &m_value.number_integer : nullptr;
  }
  JsonFloat* get_impl_ptr(JsonFloat*) noexcept {
    return bNumber() ? &m_value.number_float : nullptr;
  }
  constexpr const JsonFloat* get_impl_ptr(const JsonFloat*) const noexcept {
    return bNumber() ? &m_value.number_float : nullptr;
  }

  // ThisType 可以被推断成Json/const Json
  template <typename ReferenceType, typename ThisType>
  static ReferenceType get_ref_impl(ThisType& obj) {
    // 引用的指针类型
    using PointerType = typename std::add_pointer<ReferenceType>::type;
    auto ptr = obj.template get_ptr<PointerType>();
    if (ptr != nullptr) {
      return *ptr;
    } else {
      throw std::domain_error(
          "incompatible ReferenceType for get_ref, actual type is " +
          obj.type_name());
    }
  }
#pragma endregion
 public:
  // 返回非指针类型,int,bool,double,string
  template <typename T, typename std::enable_if<not std::is_pointer<T>::value,
                                                int>::type = 0>
  T get() const {
    return get_impl(static_cast<T*>(nullptr));
  }
  // 返回指针类型
  template <typename PointerType,
            typename std::enable_if<std::is_pointer<PointerType>::value,
                                    int>::type = 0>
  PointerType get() noexcept {
    // delegate the call to get_ptr
    return get_ptr<PointerType>();
  }
  // 返回const指针类型
  template <typename PointerType,
            typename std::enable_if<std::is_pointer<PointerType>::value,
                                    int>::type = 0>
  constexpr const PointerType get() const noexcept {
    // delegate the call to get_ptr
    return get_ptr<PointerType>();
  }
  // 统一get_impl_ptr给外部接口,要求PointerType是指针类型
  template <typename PointerType,
            typename std::enable_if<std::is_pointer<PointerType>::value,
                                    int>::type = 0>
  PointerType get_ptr() noexcept {
    // delegate the call to get_impl_ptr<>()
    return get_impl_ptr(static_cast<PointerType>(nullptr));
  }

  // 返回const指针类型
  template <
      typename PointerType,
      typename std::enable_if<std::is_pointer<PointerType>::value and
                                  std::is_const<typename std::remove_pointer<
                                      PointerType>::type>::value,
                              int>::type = 0>
  constexpr const PointerType get_ptr() const noexcept {
    // delegate the call to get_impl_ptr<>() const
    return get_impl_ptr(static_cast<const PointerType>(nullptr));
  }

  // 返回引用类型
  template <typename ReferenceType,
            typename std::enable_if<std::is_reference<ReferenceType>::value,
                                    int>::type = 0>
  ReferenceType get_ref() {
    // delegate call to get_ref_impl
    return get_ref_impl<ReferenceType>(*this);
  }
  template <
      typename ReferenceType,
      typename std::enable_if<std::is_reference<ReferenceType>::value and
                                  std::is_const<typename std::remove_reference<
                                      ReferenceType>::type>::value,
                              int>::type = 0>
  ReferenceType get_ref() const {
    // delegate call to get_ref_impl
    return get_ref_impl<ReferenceType>(*this);
  }

  // 重载类型转换，比如可以返回int,double,bool
  template <
      typename T,
      typename std::enable_if<
          not std::is_pointer<T>::value and
              not std::is_same<T, typename JsonStr::value_type>::value
#ifndef _MSC_VER  // Fix for issue #167 operator<< abiguity under VS2015
              and not std::is_same<T, std::initializer_list<
                                          typename JsonStr::value_type>>::value
#endif
          ,
          int>::type = 0>
  operator T() const {
    // delegate the call to get<>() const
    return get<T>();
  }
#pragma region array_map
  // 数组下标获取引用
  Ref at(size_type idx) {
    // at only works for arrays
    if (bArray()) {
      try {
        assert(m_value.array != nullptr);
        return m_value.array->at(idx);
      } catch (std::out_of_range&) {
        // create better exception explanation
        throw std::out_of_range("array index " + std::to_string(idx) +
                                " is out of range");
      }
    } else {
      throw std::domain_error("cannot use at() with " + type_name());
    }
  }

  ConstRef at(size_type idx) const {
    // at only works for arrays
    if (bArray()) {
      try {
        assert(m_value.array != nullptr);
        return m_value.array->at(idx);
      } catch (std::out_of_range&) {
        // create better exception explanation
        throw std::out_of_range("array index " + std::to_string(idx) +
                                " is out of range");
      }
    } else {
      throw std::domain_error("cannot use at() with " + type_name());
    }
  }
  Ref operator[](size_type idx) {
    // implicitly convert null m_value to an empty array
    if (bNull()) {
      m_type = ArgType::Array;
      m_value.array = create<JsonArray>();
    }
    // operator[] only works for arrays
    if (bArray()) {
      // fill up array with null values until given idx is reached
      assert(m_value.array != nullptr);
      // 如果当前数组长度小于等于idx,则填充null值
      for (size_t i = m_value.array->size(); i <= idx; ++i) {
        m_value.array->push_back(Json());
      }
      return m_value.array->operator[](idx);
    } else {
      throw std::domain_error("cannot use operator[] with " + type_name());
    }
  }
  ConstRef operator[](size_type idx) const {
    if (bArray()) {
      assert(m_value.array != nullptr);
      return m_value.array->operator[](idx);
    } else {
      throw std::domain_error("cannot use operator[] with " + type_name());
    }
  }
  void push_back(Json&& val) {
    // push_back only works for null objects or arrays
    if (not(bNull() or bArray())) {
      throw std::domain_error("cannot use push_back() with " + type_name());
    }

    // transform null object into an array
    if (bNull()) {
      m_type = ArgType::Array;
      m_value = ArgType::Array;
    }

    // add element to array (move semantics)
    assert(m_value.array != nullptr);
    m_value.array->push_back(std::move(val));
    // invalidate object
    val.m_type = ArgType::Null;
  }
  void push_back(const Json& val) {
    // push_back only works for null objects or arrays
    if (not(bNull() or bArray())) {
      throw std::domain_error("cannot use push_back() with " + type_name());
    }

    // transform null object into an array
    if (bNull()) {
      m_type = ArgType::Array;
      m_value = ArgType::Array;
    }

    // add element to array
    assert(m_value.array != nullptr);
    m_value.array->push_back(val);
  }
  void push_back(const typename JsonObject::value_type& val) {
    // push_back only works for null objects or objects
    if (not(bNull() or bObject())) {
      std::domain_error("cannot use push_back() with " + type_name());
    }
    // transform null object into an object
    if (bNull()) {
      m_type = ArgType::Object;
      m_value = ArgType::Object;
    }
    // add element to array
    m_value.object->insert(val);
  }
  void push_back(std::initializer_list<Json> init) {
    if (bObject() and init.size() == 2 and init.begin()->bString()) {
      const JsonStr key = *init.begin();
      push_back(typename JsonObject::value_type(key, *(init.begin() + 1)));
    } else {
      push_back(Json(init));
    }
  }

  // Map从key获取引用
  Ref at(const typename JsonObject::key_type& key) {
    // at only works for objects
    if (bObject()) {
      try {
        assert(m_value.object != nullptr);
        return m_value.object->at(key);
      } catch (std::out_of_range&) {
        // create better exception explanation
        throw std::out_of_range("key '" + key + "' not found");
      }
    } else {
      throw std::domain_error("cannot use at() with " + type_name());
    }
  }
  ConstRef at(const typename JsonObject::key_type& key) const {
    // at only works for objects
    if (bObject()) {
      try {
        assert(m_value.object != nullptr);
        return m_value.object->at(key);
      } catch (std::out_of_range&) {
        // create better exception explanation
        throw std::out_of_range("key '" + key + "' not found");
      }
    } else {
      throw std::domain_error("cannot use at() with " + type_name());
    }
  }
  // const string&
  Ref operator[](const typename JsonObject::key_type& key) {
    // implicitly convert null m_value to an empty object
    if (bNull()) {
      m_type = ArgType::Object;
      m_value.object = create<JsonObject>();
    }
    // operator[] only works for objects
    if (bObject()) {
      assert(m_value.object != nullptr);
      return m_value.object->operator[](key);
    } else {
      throw std::domain_error("cannot use operator[] with " + type_name());
    }
  }
  ConstRef operator[](const typename JsonObject::key_type& key) const {
    // operator[] only works for objects
    if (bObject()) {
      assert(m_value.object != nullptr);
      assert(m_value.object->find(key) != m_value.object->end());
      return m_value.object->find(key)->second;
    } else {
      throw std::domain_error("cannot use operator[] with " + type_name());
    }
  }
  // const char
  template <typename T>
  Ref operator[](T* key) {
    // implicitly convert null m_value to an empty object
    if (bNull()) {
      m_type = ArgType::Object;
      m_value.object = create<JsonObject>();
    }
    // operator[] only works for objects
    if (bObject()) {
      assert(m_value.object != nullptr);
      return m_value.object->operator[](key);
    } else {
      throw std::domain_error("cannot use operator[] with " + type_name());
    }
  }
  template <typename T>
  ConstRef operator[](T* key) const {
    // operator[] only works for objects
    if (bObject()) {
      assert(m_value.object != nullptr);
      assert(m_value.object->find(key) != m_value.object->end());
      return m_value.object->find(key)->second;
    } else {
      throw std::domain_error("cannot use operator[] with " + type_name());
    }
  }

  bool find(const typename JsonObject::key_type& key) const {
    // this find only works for objects
    if (bObject()) {
      assert(m_value.object != nullptr);
      return m_value.object->find(key) != m_value.object->end();
    } else {
      throw std::domain_error("cannot use find() with " + type_name());
    }
  }
  template <class T,
            typename std::enable_if<std::is_convertible<ValueType, T>::value,
                                    int>::type = 0>
  T value(const typename JsonObject::key_type& key, T default_value) const {
    // at only works for objects
    if (bObject()) {
      // if key is found, return value and given default value otherwise
      const auto it = m_value.object->find(key);
      if (it != m_value.object->end()) {
        return it->second;
      } else {
        return default_value;
      }
    } else {
      throw std::domain_error("cannot use value() with " + type_name());
    }
  }
  JsonStr value(const typename JsonObject::key_type& key,
                const char* default_value) const {
    return value(key, JsonStr(default_value));
  }
#pragma endregion

  size_type erase(const typename JsonObject::key_type& key) {
    // this erase only works for objects
    if (bObject()) {
      assert(m_value.object != nullptr);
      return m_value.object->erase(key);
    } else {
      throw std::domain_error("cannot use erase() with " + type_name());
    }
  }

  void erase(const size_type idx) {
    // this erase only works for arrays
    if (bArray()) {
      if (idx >= m_value.array->size()) {
        throw std::out_of_range("array index " + std::to_string(idx) +
                                " is out of range");
      }

      assert(m_value.array != nullptr);
      m_value.array->erase(m_value.array->begin() +
                           static_cast<std::ptrdiff_t>(idx));
    } else {
      throw std::domain_error("cannot use erase() with " + type_name());
    }
  }

  bool empty() const noexcept {
    switch (m_type) {
      case ArgType::Null: {
        // null values are empty
        return true;
      }

      case ArgType::Array: {
        assert(m_value.array != nullptr);
        return m_value.array->empty();
      }

      case ArgType::Object: {
        assert(m_value.object != nullptr);
        return m_value.object->empty();
      }

      default: {
        // all other types are nonempty
        return false;
      }
    }
    return false;
  }

  size_type size() const noexcept {
    switch (m_type) {
      case ArgType::Null: {
        // null values are empty
        return 0;
      }

      case ArgType::Array: {
        assert(m_value.array != nullptr);
        return m_value.array->size();
      }

      case ArgType::Object: {
        assert(m_value.object != nullptr);
        return m_value.object->size();
      }

      default: {
        // all other types have size 1
        return 1;
      }
    }
  }

  void clear() noexcept {
    switch (m_type) {
      case ArgType::Int: {
        m_value.number_integer = 0;
        break;
      }
      case ArgType::Number: {
        m_value.number_float = 0.0;
        break;
      }
      case ArgType::Boolean: {
        m_value.boolean = false;
        break;
      }
      case ArgType::String: {
        assert(m_value.string != nullptr);
        m_value.string->clear();
        break;
      }
      case ArgType::Array: {
        assert(m_value.array != nullptr);
        m_value.array->clear();
        break;
      }
      case ArgType::Object: {
        assert(m_value.object != nullptr);
        m_value.object->clear();
        break;
      }
      default: {
        break;
      }
    }
  }

 private:
  JsonStr type_name() const noexcept {
    switch (m_type) {
      case ArgType::Null:
        return "null";
      case ArgType::Object:
        return "object";
      case ArgType::Array:
        return "array";
      case ArgType::String:
        return "string";
      case ArgType::Boolean:
        return "boolean";
      default:
        return "number";
    }
  }

 public:
  std::string dump(int indent = -1) const {
    bool bIndent = indent >= 0;
    int32_t xindent = bIndent ? indent : 0;
    std::string result;
    switch (m_type) {
      case ArgType::Null:
        result = "null";
        break;
      case ArgType::Boolean:
        result = m_value.boolean ? "true" : "false";
        break;
      case ArgType::Int:
        result = std::to_string(m_value.number_integer);
        break;
      case ArgType::Number:
        result = std::to_string(m_value.number_float);
        break;
      case ArgType::String: {
        result = "\"";
        for (size_t i = 0; i < m_value.string->size(); ++i) {
          unsigned char c = static_cast<unsigned char>((*m_value.string)[i]);
          switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            default:
              if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                result += buf;
              } else {
                result += static_cast<char>(c);
              }
          }
        }
        result += "\"";
        break;
      }
      case ArgType::Array:
        result = bIndent ? "[\n" : "[";
        for (size_t i = 0; i < m_value.array->size(); ++i) {
          result +=
              std::string(xindent, ' ') + m_value.array->at(i).dump(indent);
          if (i != m_value.array->size() - 1) {
            result += ",";
          }
          if (bIndent) {
            result += "\n";
          }
        }
        result += std::string(xindent, ' ') + "]";
        break;
      case ArgType::Object:
        result = bIndent ? "{\n" : "{";
        for (auto it = m_value.object->begin(); it != m_value.object->end();
             ++it) {
          result += std::string(xindent, ' ') + "\"" + it->first +
                    "\":" + it->second.dump(indent);
          if (it != --m_value.object->end()) {
            result += ",";
          }
          if (bIndent) {
            result += "\n";
          }
        }
        result += std::string(xindent, ' ') + "}";
        break;
    }
    return result;
  }
};
extern "C" {
AVOX_EXPORT Json parserJson(const char* json);
AVOX_EXPORT const char* jsonToStr(const Json& json);
}

// JSON 字符串转义: 转义双引号/反斜杠/控制字符, 用于手工拼 JSON 值。
// 注意: Json::dump() 内部已做转义, 此函数供手工拼 JSON 的场景使用。
inline std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (size_t i = 0; i < s.size(); i++) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char b[8];
          snprintf(b, sizeof(b), "\\u%04x", c);
          out += b;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

}
