#ifndef MEDIA_RECORD_CONFIG_JSON_H_
#define MEDIA_RECORD_CONFIG_JSON_H_

#include <map>
#include <string>
#include <vector>

// Minimal self-contained JSON parser (spec 001).
//
// media_record templates follow the graph_runtime native JSON schema (see
// specs/001-project-architecture/research.md §6). The runtime's own JsonParser
// is internal to graph_runtime and not visible to external workspaces, so this
// tiny dependency-free parser gives examples/tests a way to load the templates.
// It supports the standard JSON value set: null / bool / number / string /
// array / object, with \uXXXX escapes and no comments.

namespace media::record::config {

enum class JsonType { kNull, kBool, kNumber, kString, kArray, kObject };

class JsonValue {
 public:
  JsonValue() = default;

  JsonType type() const { return type_; }

  bool IsNull() const { return type_ == JsonType::kNull; }
  bool IsBool() const { return type_ == JsonType::kBool; }
  bool IsNumber() const { return type_ == JsonType::kNumber; }
  bool IsString() const { return type_ == JsonType::kString; }
  bool IsArray() const { return type_ == JsonType::kArray; }
  bool IsObject() const { return type_ == JsonType::kObject; }

  bool GetBool(bool* out) const;
  bool GetNumber(double* out) const;
  bool GetString(std::string* out) const;

  const std::vector<JsonValue>& AsArray() const { return array_; }
  const std::map<std::string, JsonValue>& AsObject() const { return object_; }

  // Returns nullptr when |key| is absent or this is not an object.
  const JsonValue* Find(const std::string& key) const;

  // Convenience accessors returning defaults for missing/type-mismatched keys.
  std::string GetStringOr(const std::string& key,
                          const std::string& fallback) const;

 private:
  friend JsonValue MakeNull();
  friend JsonValue MakeBool(bool value);
  friend JsonValue MakeNumber(double value);
  friend JsonValue MakeString(std::string value);
  friend JsonValue MakeArray(std::vector<JsonValue> value);
  friend JsonValue MakeObject(std::map<std::string, JsonValue> value);
  friend bool ParseJson(const std::string& text, JsonValue* out,
                        std::string* error);

  JsonType type_ = JsonType::kNull;
  bool bool_value_ = false;
  double number_value_ = 0;
  std::string string_value_;
  std::vector<JsonValue> array_;
  std::map<std::string, JsonValue> object_;
};

// Parses |text| into |out|. Returns true on success; on failure sets |error|
// to a human-readable message (optionally nullptr).
bool ParseJson(const std::string& text, JsonValue* out, std::string* error);

}  // namespace media::record::config

#endif  // MEDIA_RECORD_CONFIG_JSON_H_
