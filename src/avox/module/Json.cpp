#include "Json.hpp"
namespace avox {

// 参考 https://zhuanlan.zhihu.com/p/630884423

class JsonParser {
 public:
  JsonParser(const std::string& json) : json_(json), pos_(0) {}

  Json parse() {
    skipWhiteSpace();
    char c = json_[pos_];
    if (c == '{') {
      return parseObject();
    } else if (c == '[') {
      return parseArray();
    } else if (c == '\"') {
      return parseString();
    } else if (c == 't' || c == 'f') {
      return parseBoolean();
    } else if (c == 'n') {
      return parseNull();
    } else {
      return parseNumber();
    }
  }

 private:
  std::string json_;
  size_t pos_;

  void skipWhiteSpace() {
    while (pos_ < json_.size() && std::isspace(json_[pos_])) {
      ++pos_;
    }
  }

  Json parseObject() {
    Json result(ArgType::Object);
    ++pos_;
    skipWhiteSpace();
    while (json_[pos_] != '}') {
      const auto key = parseString();
      skipWhiteSpace();
      ++pos_;
      result[key] = std::move(parse());
      skipWhiteSpace();
      if (json_[pos_] == ',') {
        ++pos_;
        skipWhiteSpace();
      }
    }
    ++pos_;
    return result;
  }

  Json parseArray() {
    // 必须显式初始化成 Array: 空数组 "[]" 不会进循环, 默认构造的 Null 会被原样
    // 返回, 于是 [] 被解析成 null (调用方的 bArray() 全部误判)。
    Json result(ArgType::Array);
    ++pos_;
    skipWhiteSpace();
    while (json_[pos_] != ']') {
      result.push_back(std::move(parse()));
      skipWhiteSpace();
      if (json_[pos_] == ',') {
        ++pos_;
        skipWhiteSpace();
      }
    }
    ++pos_;
    return result;
  }

  std::string parseString() {
    std::string result;
    ++pos_;
    while (json_[pos_] != '\"') {
      if (json_[pos_] == '\\') {
        ++pos_;
        if (json_[pos_] == '\"') {
          result += '\"';
        } else if (json_[pos_] == '\\') {
          result += '\\';
        } else if (json_[pos_] == '/') {
          result += '/';
        } else if (json_[pos_] == 'b') {
          result += '\b';
        } else if (json_[pos_] == 'f') {
          result += '\f';
        } else if (json_[pos_] == 'n') {
          result += '\n';
        } else if (json_[pos_] == 'r') {
          result += '\r';
        } else if (json_[pos_] == 't') {
          result += '\t';
        } else if (json_[pos_] == 'u') {
          // \uXXXX — 解析 4 位十六进制, 转 UTF-8
          if (pos_ + 4 < json_.size()) {
            char hex[5] = {json_[pos_ + 1], json_[pos_ + 2],
                           json_[pos_ + 3], json_[pos_ + 4], '\0'};
            unsigned long cp = strtoul(hex, nullptr, 16);
            if (cp < 0x80) {
              result += static_cast<char>(cp);
            } else if (cp < 0x800) {
              result += static_cast<char>(0xC0 | (cp >> 6));
              result += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
              // 代理对 (0xD800~0xDFFF) 暂不处理, 直接按 3 字节编码
              result += static_cast<char>(0xE0 | (cp >> 12));
              result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
              result += static_cast<char>(0x80 | (cp & 0x3F));
            }
            pos_ += 4;  // 跳过 XXXX, 循环末尾还有 ++pos_
          }
        }
      } else {
        result.append(1, json_[pos_]);
      }
      ++pos_;
    }
    ++pos_;
    return result;
  }

  Json parseBoolean() {
    if (json_[pos_] == 't') {
      pos_ += 4;
      return Json(true);
    } else {
      pos_ += 5;
      return Json(false);
    }
  }

  Json parseNull() {
    pos_ += 4;
    return Json();
  }

  Json parseNumber() {
    size_t start = pos_;
    bool bDouble = false;
    while (pos_ < json_.size() &&
           (std::isdigit(json_[pos_]) || json_[pos_] == '.' ||
            json_[pos_] == 'e' || json_[pos_] == 'E' || json_[pos_] == '+' ||
            json_[pos_] == '-')) {
      if (json_[pos_] == '.') {
        bDouble = true;
      }
      ++pos_;
    }
    std::string numberStr = json_.substr(start, pos_ - start);
    if (numberStr.empty() || numberStr == "-" || numberStr == "+") {
      return Json(0);
    }
    if (bDouble) {
      size_t idx = 0;
      double number = std::stod(numberStr, &idx);
      if (idx == 0) return Json(0.0);
      return Json(number);
    } else {
      size_t idx = 0;
      int64_t number = std::stoll(numberStr, &idx, 10);
      if (idx == 0) return Json((int64_t)0);
      return Json(number);
    }
  }
};

Json parserJson(const char* json) {
  JsonParser parser(json);
  Json vlaue = parser.parse();
  return vlaue;
}

const char* jsonToStr(const Json& json) { return json.dump().c_str(); }

}
