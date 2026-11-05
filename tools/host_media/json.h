// SPDX-License-Identifier: GPL-2.0-or-later
// A small JSON value, reader and writer for Kodi's JSON-RPC
// (tools/host_media/kodi.h). Numbers are doubles, objects keep their order.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>
namespace HostMediaJson
{
struct Value
{
    enum Type { Nothing, Boolean, Number, String, Array, Object } type = Nothing;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> members;

    Value() = default;
    Value(bool b) : type(Boolean), boolean(b) {}
    Value(int n) : type(Number), number(n) {}
    Value(unsigned n) : type(Number), number(n) {}
    Value(double n) : type(Number), number(n) {}
    Value(const char *s) : type(String), text(s) {}
    Value(std::string s) : type(String), text(std::move(s)) {}
    static Value MakeArray() { Value v; v.type = Array; return v; }
    static Value MakeObject() { Value v; v.type = Object; return v; }

    bool IsNull() const { return type == Nothing; }
    // A member of an object, or a null value.
    const Value &operator[](const char *key) const
    {
        static const Value none;
        if (type != Object) return none;
        for (const auto &m : members) if (m.first == key) return m.second;
        return none;
    }
    const Value &At(size_t i) const { static const Value none; return type == Array && i < items.size() ? items[i] : none; }
    Value &Set(const char *key, Value v)
    {
        type = Object;
        for (auto &m : members) if (m.first == key) { m.second = std::move(v); return m.second; }
        members.emplace_back(key, std::move(v));
        return members.back().second;
    }
    Value &Add(Value v) { type = Array; items.push_back(std::move(v)); return items.back(); }
    std::string Text(const char *fallback = "") const { return type == String ? text : fallback; }
    double Num(double fallback = 0) const { return type == Number ? number : fallback; }
    bool Truth() const { return type == Boolean ? boolean : type == Number ? number != 0 : false; }
    size_t Size() const { return type == Array ? items.size() : type == Object ? members.size() : 0; }
};

inline void Quote(std::string &out, const std::string &s)
{
    out += '"';
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
            else out += char(c);
        }
    }
    out += '"';
}

inline void Write(std::string &out, const Value &v)
{
    switch (v.type)
    {
    case Value::Nothing: out += "null"; break;
    case Value::Boolean: out += v.boolean ? "true" : "false"; break;
    case Value::Number:
    {
        char b[32];
        if (std::isfinite(v.number) && v.number == std::floor(v.number) && std::fabs(v.number) < 1e15) snprintf(b, sizeof b, "%.0f", v.number);
        else if (std::isfinite(v.number))
        {
            snprintf(b, sizeof b, "%.17g", v.number);
            for (char *c = b; *c; ++c) if (*c == ',') *c = '.';  // A locale with a decimal comma.
        }
        else snprintf(b, sizeof b, "null");
        out += b;
        break;
    }
    case Value::String: Quote(out, v.text); break;
    case Value::Array:
        out += '[';
        for (size_t i = 0; i < v.items.size(); ++i) { if (i) out += ','; Write(out, v.items[i]); }
        out += ']';
        break;
    case Value::Object:
        out += '{';
        for (size_t i = 0; i < v.members.size(); ++i)
        {
            if (i) out += ',';
            Quote(out, v.members[i].first); out += ':'; Write(out, v.members[i].second);
        }
        out += '}';
        break;
    }
}
inline std::string Dump(const Value &v) { std::string out; Write(out, v); return out; }

class Reader
{
    const std::string &s;
    size_t at;
    unsigned depth = 0;
    void Space() { while (at < s.size() && (s[at] == ' ' || s[at] == '\t' || s[at] == '\n' || s[at] == '\r')) ++at; }
    bool Literal(const char *word)
    {
        size_t n = 0; while (word[n]) ++n;
        if (s.compare(at, n, word) != 0) return false;
        at += n; return true;
    }
    static void Utf8(std::string &out, unsigned c)
    {
        if (c < 0x80) out += char(c);
        else if (c < 0x800) { out += char(0xc0 | c >> 6); out += char(0x80 | (c & 63)); }
        else if (c < 0x10000) { out += char(0xe0 | c >> 12); out += char(0x80 | (c >> 6 & 63)); out += char(0x80 | (c & 63)); }
        else { out += char(0xf0 | c >> 18); out += char(0x80 | (c >> 12 & 63)); out += char(0x80 | (c >> 6 & 63)); out += char(0x80 | (c & 63)); }
    }
    bool Hex4(unsigned &c)
    {
        if (at + 4 > s.size()) return false;
        c = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            const char h = s[at++];
            c <<= 4;
            if (h >= '0' && h <= '9') c |= h - '0';
            else if (h >= 'a' && h <= 'f') c |= h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') c |= h - 'A' + 10;
            else return false;
        }
        return true;
    }
    bool Str(std::string &out)
    {
        if (at >= s.size() || s[at] != '"') return false;
        ++at;
        while (at < s.size())
        {
            const char c = s[at++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (at >= s.size()) return false;
            const char e = s[at++];
            switch (e)
            {
            case '"': case '\\': case '/': out += e; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':
            {
                unsigned c1 = 0;
                if (!Hex4(c1)) return false;
                if (c1 >= 0xd800 && c1 < 0xdc00 && s.compare(at, 2, "\\u") == 0)
                {
                    at += 2; unsigned c2 = 0;
                    if (!Hex4(c2)) return false;
                    if (c2 >= 0xdc00 && c2 < 0xe000) c1 = 0x10000 + ((c1 - 0xd800) << 10) + (c2 - 0xdc00);
                    else { Utf8(out, 0xfffd); c1 = c2; }
                }
                Utf8(out, c1);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    // A number by JSON's grammar, independent of the locale: GTK sets the
    // user's, and strtod would read "1," as one in German.
    bool Number(Value &v)
    {
        const size_t start = at;
        bool negative = false;
        if (at < s.size() && s[at] == '-') { negative = true; ++at; }
        auto digit = [&] { return at < s.size() && s[at] >= '0' && s[at] <= '9'; };
        if (!digit()) { at = start; return false; }
        double n = 0;
        while (digit()) n = n * 10 + (s[at++] - '0');
        if (at < s.size() && s[at] == '.')
        {
            ++at;
            if (!digit()) { at = start; return false; }
            double scale = 0.1;
            while (digit()) { n += (s[at++] - '0') * scale; scale /= 10; }
        }
        if (at < s.size() && (s[at] == 'e' || s[at] == 'E'))
        {
            ++at;
            bool down = false;
            if (at < s.size() && (s[at] == '+' || s[at] == '-')) down = s[at++] == '-';
            if (!digit()) { at = start; return false; }
            int exponent = 0;
            while (digit()) exponent = std::min(exponent * 10 + (s[at++] - '0'), 400);
            n *= std::pow(10.0, down ? -exponent : exponent);
        }
        v = Value(negative ? -n : n);
        return true;
    }
    bool Parse(Value &v)
    {
        if (++depth > 64) return false;
        Space();
        if (at >= s.size()) return false;
        bool okay = false;
        const char c = s[at];
        if (c == '{')
        {
            ++at; v = Value::MakeObject(); Space();
            if (at < s.size() && s[at] == '}') { ++at; okay = true; }
            else for (;;)
            {
                Space(); std::string key;
                if (!Str(key)) break;
                Space(); if (at >= s.size() || s[at] != ':') break; ++at;
                Value item; if (!Parse(item)) break;
                v.members.emplace_back(std::move(key), std::move(item));
                Space(); if (at >= s.size()) break;
                if (s[at] == ',') { ++at; continue; }
                if (s[at] == '}') { ++at; okay = true; }
                break;
            }
        }
        else if (c == '[')
        {
            ++at; v = Value::MakeArray(); Space();
            if (at < s.size() && s[at] == ']') { ++at; okay = true; }
            else for (;;)
            {
                Value item; if (!Parse(item)) break;
                v.items.push_back(std::move(item));
                Space(); if (at >= s.size()) break;
                if (s[at] == ',') { ++at; continue; }
                if (s[at] == ']') { ++at; okay = true; }
                break;
            }
        }
        else if (c == '"') { v = Value(std::string()); okay = Str(v.text); }
        else if (Literal("true")) { v = Value(true); okay = true; }
        else if (Literal("false")) { v = Value(false); okay = true; }
        else if (Literal("null")) { v = Value(); okay = true; }
        else okay = Number(v);
        --depth;
        return okay;
    }
public:
    Reader(const std::string &text, size_t from = 0) : s(text), at(from) {}
    bool Read(Value &v) { return Parse(v); }
    size_t Position() const { return at; }
};
inline bool Parse(const std::string &text, Value &v)
{
    Reader r(text);
    if (!r.Read(v)) return false;
    size_t at = r.Position();
    while (at < text.size() && (text[at] == ' ' || text[at] == '\n' || text[at] == '\r' || text[at] == '\t')) ++at;
    return at == text.size();
}

// Kodi sends JSON-RPC over TCP without delimiters: the end of a message is
// the closing brace of its outermost object. Returns the length of the
// first complete message in `buffer` (0 if it is not complete yet).
inline size_t MessageLength(const std::string &buffer)
{
    unsigned depth = 0; bool string = false, escape = false, started = false;
    for (size_t i = 0; i < buffer.size(); ++i)
    {
        const char c = buffer[i];
        if (string)
        {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') string = false;
            continue;
        }
        if (c == '"') string = true;
        else if (c == '{' || c == '[') { ++depth; started = true; }
        else if ((c == '}' || c == ']') && depth && !--depth && started) return i + 1;
    }
    return 0;
}
}
