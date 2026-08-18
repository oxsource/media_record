#include "src/framework/config/json.h"

#include <cctype>
#include <cstdint>
#include <string>

namespace media::record::config {

JsonValue MakeNull() {
  JsonValue value;
  return value;
}

JsonValue MakeBool(bool flag) {
  JsonValue value;
  value.type_ = JsonType::kBool;
  value.bool_value_ = flag;
  return value;
}

JsonValue MakeNumber(double number) {
  JsonValue value;
  value.type_ = JsonType::kNumber;
  value.number_value_ = number;
  return value;
}

JsonValue MakeString(std::string text) {
  JsonValue value;
  value.type_ = JsonType::kString;
  value.string_value_ = std::move(text);
  return value;
}

JsonValue MakeArray(std::vector<JsonValue> array) {
  JsonValue value;
  value.type_ = JsonType::kArray;
  value.array_ = std::move(array);
  return value;
}

JsonValue MakeObject(std::map<std::string, JsonValue> object) {
  JsonValue value;
  value.type_ = JsonType::kObject;
  value.object_ = std::move(object);
  return value;
}

namespace {

// Parser state: walks |text| with an index and reports errors with the offset.
struct Parser {
  const std::string& text;
  size_t pos = 0;

  explicit Parser(const std::string& t) : text(t) {}

  bool AtEnd() const { return pos >= text.size(); }

  void SkipWhitespace() {
    while (pos < text.size() &&
           (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' ||
            text[pos] == '\n')) {
      ++pos;
    }
  }

  bool Consume(char c) {
    SkipWhitespace();
    if (!AtEnd() && text[pos] == c) {
      ++pos;
      return true;
    }
    return false;
  }

  std::string Error(const std::string& what) const {
    return "json error at offset " + std::to_string(pos) + ": " + what;
  }
};

bool ParseValue(Parser& p, JsonValue* out, std::string* error);

// Parses a \"...\" string into |out| (without quotes). Handles the standard
// escapes including \uXXXX.
bool ParseStringRaw(Parser& p, std::string* out, std::string* error) {
  if (p.AtEnd() || p.text[p.pos] != '"') {
    *error = p.Error("expected '\"'");
    return false;
  }
  ++p.pos;

  std::string result;
  while (true) {
    if (p.AtEnd()) {
      *error = p.Error("unterminated string");
      return false;
    }
    char c = p.text[p.pos];
    if (c == '"') {
      ++p.pos;
      *out = std::move(result);
      return true;
    }
    if (c == '\\') {
      ++p.pos;
      if (p.AtEnd()) {
        *error = p.Error("unterminated escape sequence");
        return false;
      }
      char esc = p.text[p.pos];
      switch (esc) {
        case '"':
          result.push_back('"');
          break;
        case '\\':
          result.push_back('\\');
          break;
        case '/':
          result.push_back('/');
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'u': {
          if (p.pos + 4 >= p.text.size()) {
            *error = p.Error("truncated \\u escape");
            return false;
          }
          uint32_t code = 0;
          for (int i = 0; i < 4; ++i) {
            char h = p.text[p.pos + 1 + i];
            int nibble = -1;
            if (h >= '0' && h <= '9')
              nibble = h - '0';
            else if (h >= 'a' && h <= 'f')
              nibble = h - 'a' + 10;
            else if (h >= 'A' && h <= 'F')
              nibble = h - 'A' + 10;
            if (nibble < 0) {
              *error = p.Error("invalid \\u escape");
              return false;
            }
            code = (code << 4) | static_cast<uint32_t>(nibble);
          }
          p.pos += 4;
          // Encode as UTF-8 (BMP only; surrogate pairs are not needed by the
          // pipeline templates but simple pairs are still handled).
          if (code >= 0xD800 && code <= 0xDBFF && p.pos + 6 < p.text.size() &&
              p.text[p.pos + 1] == '\\' && p.text[p.pos + 2] == 'u') {
            uint32_t low = 0;
            bool ok_low = true;
            for (int i = 0; i < 4; ++i) {
              char h = p.text[p.pos + 3 + i];
              int nibble = -1;
              if (h >= '0' && h <= '9')
                nibble = h - '0';
              else if (h >= 'a' && h <= 'f')
                nibble = h - 'a' + 10;
              else if (h >= 'A' && h <= 'F')
                nibble = h - 'A' + 10;
              if (nibble < 0) {
                ok_low = false;
                break;
              }
              low = (low << 4) | static_cast<uint32_t>(nibble);
            }
            if (ok_low && low >= 0xDC00 && low <= 0xDFFF) {
              code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
              p.pos += 6;
            }
          }
          if (code <= 0x7F) {
            result.push_back(static_cast<char>(code));
          } else if (code <= 0x7FF) {
            result.push_back(static_cast<char>(0xC0 | (code >> 6)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else if (code <= 0xFFFF) {
            result.push_back(static_cast<char>(0xE0 | (code >> 12)));
            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else {
            result.push_back(static_cast<char>(0xF0 | (code >> 18)));
            result.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          }
          break;
        }
        default:
          *error = p.Error("invalid escape character");
          return false;
      }
      ++p.pos;
      continue;
    }
    result.push_back(c);
    ++p.pos;
  }
}

bool ParseNumberRaw(Parser& p, double* out, std::string* error) {
  size_t start = p.pos;
  if (!p.AtEnd() && p.text[p.pos] == '-') ++p.pos;
  if (p.AtEnd() || !std::isdigit(static_cast<unsigned char>(p.text[p.pos]))) {
    *error = p.Error("invalid number");
    return false;
  }
  while (!p.AtEnd() && std::isdigit(static_cast<unsigned char>(p.text[p.pos])))
    ++p.pos;
  if (!p.AtEnd() && p.text[p.pos] == '.') {
    ++p.pos;
    if (p.AtEnd() || !std::isdigit(static_cast<unsigned char>(p.text[p.pos]))) {
      *error = p.Error("expected digits after '.'");
      return false;
    }
    while (!p.AtEnd() &&
           std::isdigit(static_cast<unsigned char>(p.text[p.pos])))
      ++p.pos;
  }
  if (!p.AtEnd() && (p.text[p.pos] == 'e' || p.text[p.pos] == 'E')) {
    ++p.pos;
    if (!p.AtEnd() && (p.text[p.pos] == '+' || p.text[p.pos] == '-')) ++p.pos;
    if (p.AtEnd() || !std::isdigit(static_cast<unsigned char>(p.text[p.pos]))) {
      *error = p.Error("expected exponent digits");
      return false;
    }
    while (!p.AtEnd() &&
           std::isdigit(static_cast<unsigned char>(p.text[p.pos])))
      ++p.pos;
  }
  std::string literal = p.text.substr(start, p.pos - start);
  *out = std::stod(literal);
  return true;
}

bool ParseArray(Parser& p, std::vector<JsonValue>* out, std::string* error) {
  ++p.pos;  // consume '['
  p.SkipWhitespace();
  if (p.Consume(']')) return true;
  while (true) {
    p.SkipWhitespace();
    JsonValue value;
    if (!ParseValue(p, &value, error)) return false;
    out->push_back(std::move(value));
    if (p.Consume(']')) return true;
    if (!p.Consume(',')) {
      *error = p.Error("expected ',' or ']'");
      return false;
    }
  }
}

bool ParseObject(Parser& p, std::map<std::string, JsonValue>* out,
                 std::string* error) {
  ++p.pos;  // consume '{'
  p.SkipWhitespace();
  if (p.Consume('}')) return true;
  while (true) {
    p.SkipWhitespace();
    if (p.AtEnd() || p.text[p.pos] != '"') {
      *error = p.Error("expected string key");
      return false;
    }
    std::string key;
    if (!ParseStringRaw(p, &key, error)) return false;
    if (!p.Consume(':')) {
      *error = p.Error("expected ':'");
      return false;
    }
    p.SkipWhitespace();
    JsonValue value;
    if (!ParseValue(p, &value, error)) return false;
    (*out)[key] = std::move(value);
    if (p.Consume('}')) return true;
    if (!p.Consume(',')) {
      *error = p.Error("expected ',' or '}'");
      return false;
    }
  }
}

bool ParseValue(Parser& p, JsonValue* out, std::string* error) {
  p.SkipWhitespace();
  if (p.AtEnd()) {
    *error = p.Error("unexpected end of input");
    return false;
  }
  char c = p.text[p.pos];
  switch (c) {
    case '{': {
      std::map<std::string, JsonValue> object;
      if (!ParseObject(p, &object, error)) return false;
      *out = MakeObject(std::move(object));
      return true;
    }
    case '[': {
      std::vector<JsonValue> array;
      if (!ParseArray(p, &array, error)) return false;
      *out = MakeArray(std::move(array));
      return true;
    }
    case '"': {
      std::string value;
      if (!ParseStringRaw(p, &value, error)) return false;
      *out = MakeString(std::move(value));
      return true;
    }
    case 't':
      if (p.text.compare(p.pos, 4, "true") == 0) {
        p.pos += 4;
        *out = MakeBool(true);
        return true;
      }
      *error = p.Error("invalid literal");
      return false;
    case 'f':
      if (p.text.compare(p.pos, 5, "false") == 0) {
        p.pos += 5;
        *out = MakeBool(false);
        return true;
      }
      *error = p.Error("invalid literal");
      return false;
    case 'n':
      if (p.text.compare(p.pos, 4, "null") == 0) {
        p.pos += 4;
        *out = MakeNull();
        return true;
      }
      *error = p.Error("invalid literal");
      return false;
    default:
      if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
        double number = 0;
        if (!ParseNumberRaw(p, &number, error)) return false;
        *out = MakeNumber(number);
        return true;
      }
      *error = p.Error(std::string("unexpected character '") + c + "'");
      return false;
  }
}

}  // namespace

bool JsonValue::GetBool(bool* out) const {
  if (!IsBool()) return false;
  *out = bool_value_;
  return true;
}

bool JsonValue::GetNumber(double* out) const {
  if (!IsNumber()) return false;
  *out = number_value_;
  return true;
}

bool JsonValue::GetString(std::string* out) const {
  if (!IsString()) return false;
  *out = string_value_;
  return true;
}

const JsonValue* JsonValue::Find(const std::string& key) const {
  if (!IsObject()) return nullptr;
  auto it = object_.find(key);
  return it == object_.end() ? nullptr : &it->second;
}

std::string JsonValue::GetStringOr(const std::string& key,
                                   const std::string& fallback) const {
  const JsonValue* value = Find(key);
  std::string result;
  if (value != nullptr && value->GetString(&result)) return result;
  return fallback;
}

bool ParseJson(const std::string& text, JsonValue* out, std::string* error) {
  Parser p(text);
  p.SkipWhitespace();
  if (p.AtEnd()) {
    if (error != nullptr) *error = "json error: empty input";
    return false;
  }
  if (!ParseValue(p, out, error)) return false;
  p.SkipWhitespace();
  if (!p.AtEnd()) {
    if (error != nullptr) *error = p.Error("trailing content after document");
    return false;
  }
  return true;
}

}  // namespace media::record::config
