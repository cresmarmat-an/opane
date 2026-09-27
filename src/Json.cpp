#include "Json.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace opane::detail
{
namespace
{

class Reader
{
public:
    explicit Reader(const std::string& text) : m_Text(text) {}

    bool ReadDocument(JsonValue& out, std::string& error)
    {
        if (!ReadValue(out, 0))
        {
            error = m_Error;
            return false;
        }
        SkipSpace();
        if (m_Position < m_Text.size())
        {
            Fail("there is more after the end of the document");
            error = m_Error;
            return false;
        }
        return true;
    }

private:
    // Nesting this deep is a mistake or an attack, and either way would
    // otherwise run the stack out.
    static constexpr int MaximumDepth = 256;

    bool Fail(const char* what)
    {
        if (m_Error.empty())
        {
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), "line %d, column %d: %s", m_Line, m_Column, what);
            m_Error = buffer;
        }
        return false;
    }

    char Peek() const { return m_Position < m_Text.size() ? m_Text[m_Position] : '\0'; }

    char Next()
    {
        const char c = Peek();
        if (m_Position < m_Text.size())
        {
            ++m_Position;
            if (c == '\n')
            {
                ++m_Line;
                m_Column = 1;
            }
            else
            {
                ++m_Column;
            }
        }
        return c;
    }

    void SkipSpace()
    {
        for (;;)
        {
            const char c = Peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                Next();
            }
            else if (c == '/' && m_Position + 1 < m_Text.size() && m_Text[m_Position + 1] == '/')
            {
                while (Peek() != '\n' && Peek() != '\0')
                {
                    Next();
                }
            }
            else if (c == '/' && m_Position + 1 < m_Text.size() && m_Text[m_Position + 1] == '*')
            {
                Next();
                Next();
                while (Peek() != '\0' && !(Peek() == '*' && m_Position + 1 < m_Text.size() &&
                                           m_Text[m_Position + 1] == '/'))
                {
                    Next();
                }
                Next();
                Next();
            }
            else
            {
                return;
            }
        }
    }

    bool ReadValue(JsonValue& out, int depth)
    {
        if (depth > MaximumDepth)
        {
            return Fail("the document is nested too deeply");
        }

        SkipSpace();
        out = JsonValue{};
        out.Line = m_Line;
        out.Column = m_Column;

        const char c = Peek();
        if (c == '{')
        {
            return ReadObject(out, depth);
        }
        if (c == '[')
        {
            return ReadArray(out, depth);
        }
        if (c == '"')
        {
            out.Type = JsonValue::Kind::String;
            return ReadString(out.String);
        }
        if (c == '-' || (c >= '0' && c <= '9'))
        {
            return ReadNumber(out);
        }
        if (Match("true"))
        {
            out.Type = JsonValue::Kind::Bool;
            out.Bool = true;
            return true;
        }
        if (Match("false"))
        {
            out.Type = JsonValue::Kind::Bool;
            return true;
        }
        if (Match("null"))
        {
            return true;
        }
        return Fail(c == '\0' ? "the document ends where a value was expected" : "expected a value here");
    }

    bool Match(const char* word)
    {
        const size_t length = std::strlen(word);
        if (m_Text.compare(m_Position, length, word) != 0)
        {
            return false;
        }
        for (size_t index = 0; index < length; ++index)
        {
            Next();
        }
        return true;
    }

    bool ReadObject(JsonValue& out, int depth)
    {
        out.Type = JsonValue::Kind::Object;
        Next(); // {
        SkipSpace();
        if (Peek() == '}')
        {
            Next();
            return true;
        }
        for (;;)
        {
            SkipSpace();
            if (Peek() != '"')
            {
                return Fail("expected a name in quotes");
            }
            std::string key;
            if (!ReadString(key))
            {
                return false;
            }
            SkipSpace();
            if (Next() != ':')
            {
                return Fail("expected ':' after the name");
            }
            JsonValue value;
            if (!ReadValue(value, depth + 1))
            {
                return false;
            }
            out.Object.emplace_back(std::move(key), std::move(value));

            SkipSpace();
            const char c = Next();
            if (c == '}')
            {
                return true;
            }
            if (c != ',')
            {
                return Fail("expected ',' or '}'");
            }
            SkipSpace();
            if (Peek() == '}') // a trailing comma
            {
                Next();
                return true;
            }
        }
    }

    bool ReadArray(JsonValue& out, int depth)
    {
        out.Type = JsonValue::Kind::Array;
        Next(); // [
        SkipSpace();
        if (Peek() == ']')
        {
            Next();
            return true;
        }
        for (;;)
        {
            JsonValue value;
            if (!ReadValue(value, depth + 1))
            {
                return false;
            }
            out.Array.push_back(std::move(value));

            SkipSpace();
            const char c = Next();
            if (c == ']')
            {
                return true;
            }
            if (c != ',')
            {
                return Fail("expected ',' or ']'");
            }
            SkipSpace();
            if (Peek() == ']')
            {
                Next();
                return true;
            }
        }
    }

    static void AppendUtf8(std::string& out, unsigned codepoint)
    {
        if (codepoint < 0x80)
        {
            out += static_cast<char>(codepoint);
        }
        else if (codepoint < 0x800)
        {
            out += static_cast<char>(0xC0 | (codepoint >> 6));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else if (codepoint < 0x10000)
        {
            out += static_cast<char>(0xE0 | (codepoint >> 12));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (codepoint >> 18));
            out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }

    bool ReadHex4(unsigned& out)
    {
        out = 0;
        for (int index = 0; index < 4; ++index)
        {
            const char c = Next();
            out <<= 4;
            if (c >= '0' && c <= '9')
            {
                out |= static_cast<unsigned>(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                out |= static_cast<unsigned>(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                out |= static_cast<unsigned>(c - 'A' + 10);
            }
            else
            {
                return Fail("expected four hexadecimal digits after \\u");
            }
        }
        return true;
    }

    bool ReadString(std::string& out)
    {
        Next(); // "
        for (;;)
        {
            const char c = Next();
            if (c == '\0')
            {
                return Fail("a string is not closed");
            }
            if (c == '"')
            {
                return true;
            }
            if (c == '\n')
            {
                return Fail("a string runs past the end of its line");
            }
            if (c != '\\')
            {
                out += c;
                continue;
            }
            const char escape = Next();
            switch (escape)
            {
            case '"':
            case '\\':
            case '/':
                out += escape;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
            {
                unsigned codepoint = 0;
                if (!ReadHex4(codepoint))
                {
                    return false;
                }
                // A surrogate pair spells one character beyond the first
                // 65536.
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF && Peek() == '\\')
                {
                    Next();
                    if (Next() != 'u')
                    {
                        return Fail("expected the second half of a surrogate pair");
                    }
                    unsigned low = 0;
                    if (!ReadHex4(low))
                    {
                        return false;
                    }
                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                }
                AppendUtf8(out, codepoint);
                break;
            }
            default:
                return Fail("an unknown escape in a string");
            }
        }
    }

    bool ReadNumber(JsonValue& out)
    {
        const size_t start = m_Position;
        if (Peek() == '-')
        {
            Next();
        }
        while ((Peek() >= '0' && Peek() <= '9') || Peek() == '.' || Peek() == 'e' || Peek() == 'E' ||
               Peek() == '+' || Peek() == '-')
        {
            Next();
        }
        const std::string text = m_Text.substr(start, m_Position - start);
        char* end = nullptr;
        out.Type = JsonValue::Kind::Number;
        out.Number = std::strtod(text.c_str(), &end);
        if (end == nullptr || *end != '\0' || text == "-")
        {
            return Fail("this is not a number");
        }
        return true;
    }

    const std::string& m_Text;
    size_t m_Position = 0;
    int m_Line = 1;
    int m_Column = 1;
    std::string m_Error;
};

} // namespace

const JsonValue* JsonValue::Find(const char* key) const
{
    if (Type != Kind::Object)
    {
        return nullptr;
    }
    for (const auto& member : Object)
    {
        if (member.first == key)
        {
            return &member.second;
        }
    }
    return nullptr;
}

bool ParseJson(const std::string& text, JsonValue& outValue, std::string& outError)
{
    Reader reader(text);
    return reader.ReadDocument(outValue, outError);
}

} // namespace opane::detail
