// A small JSON reader, for theme files. Not installed and not part of the public
// API.
//
// It reads standard JSON, and forgives what people type into a file they edit
// by hand: comments, both // and /* */, and a comma after the last item. An
// error names the line and column it was found at.

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace opane::detail
{

struct JsonValue
{
    enum class Kind
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Kind Type = Kind::Null;
    bool Bool = false;
    double Number = 0.0;
    std::string String;
    std::vector<JsonValue> Array;
    std::vector<std::pair<std::string, JsonValue>> Object; // in the file's order

    // Where it started in the file, for messages about it.
    int Line = 0;
    int Column = 0;

    bool IsNull() const { return Type == Kind::Null; }
    bool IsBool() const { return Type == Kind::Bool; }
    bool IsNumber() const { return Type == Kind::Number; }
    bool IsString() const { return Type == Kind::String; }
    bool IsArray() const { return Type == Kind::Array; }
    bool IsObject() const { return Type == Kind::Object; }

    // A member of an object, or null when there is none or this is not one.
    const JsonValue* Find(const char* key) const;
};

// False, with outError saying where and what, when the text is not JSON.
bool ParseJson(const std::string& text, JsonValue& outValue, std::string& outError);

} // namespace opane::detail
