#ifndef PATTERN_CORE_JSON_INTERNAL_H
#define PATTERN_CORE_JSON_INTERNAL_H

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

namespace pc {
namespace internal {

struct JValue {
    enum Type { NUL, BOOL, INT, STR, ARR, OBJ } type = NUL;
    bool b = false;
    int64_t i = 0;
    std::string s;
    std::vector<JValue> a;
    std::vector<std::pair<std::string, JValue>> o;

    const JValue *get(const std::string &key) const {
        for (const auto &kv : o)
            if (kv.first == key)
                return &kv.second;
        return nullptr;
    }
};

struct JsonParser {
    const std::string &text;
    size_t pos = 0;
    std::string error;

    explicit JsonParser(const std::string &t) : text(t) {}

    void skip_ws() {
        while (pos < text.size()) {
            char c = text[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos;
            else
                break;
        }
    }

    bool fail(const std::string &msg) {
        if (error.empty())
            error = msg;
        return false;
    }

    bool parse_string(std::string *out) {
        if (pos >= text.size() || text[pos] != '"')
            return fail("expected string");
        ++pos;
        out->clear();
        while (pos < text.size()) {
            char c = text[pos++];
            if (c == '"')
                return true;
            if (c == '\\') {
                if (pos >= text.size())
                    return fail("bad escape");
                char e = text[pos++];
                switch (e) {
                case '"': out->push_back('"'); break;
                case '\\': out->push_back('\\'); break;
                case '/': out->push_back('/'); break;
                case 'b': out->push_back('\b'); break;
                case 'f': out->push_back('\f'); break;
                case 'n': out->push_back('\n'); break;
                case 'r': out->push_back('\r'); break;
                case 't': out->push_back('\t'); break;
                case 'u': {
                    if (pos + 4 > text.size())
                        return fail("bad unicode escape");
                    unsigned v = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = text[pos++];
                        v <<= 4;
                        if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                        else return fail("bad unicode escape");
                    }
                    if (v < 0x80) {
                        out->push_back((char)v);
                    } else {
                        /* UTF-8 encode the BMP code point */
                        if (v < 0x800) {
                            out->push_back((char)(0xC0 | (v >> 6)));
                            out->push_back((char)(0x80 | (v & 0x3F)));
                        } else {
                            out->push_back((char)(0xE0 | (v >> 12)));
                            out->push_back((char)(0x80 | ((v >> 6) & 0x3F)));
                            out->push_back((char)(0x80 | (v & 0x3F)));
                        }
                    }
                    break;
                }
                default: return fail("bad escape");
                }
            } else {
                out->push_back(c);
            }
        }
        return fail("unterminated string");
    }

    bool parse_number(JValue *out) {
        size_t start = pos;
        if (pos < text.size() && text[pos] == '-')
            ++pos;
        if (pos >= text.size() || text[pos] < '0' || text[pos] > '9')
            return fail("expected number");
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
            ++pos;
        if (pos < text.size() && (text[pos] == '.' || text[pos] == 'e' ||
                                  text[pos] == 'E'))
            return fail("non-integer number");
        std::string tok = text.substr(start, pos - start);
        try {
            out->type = JValue::INT;
            out->i = std::stoll(tok);
        } catch (...) {
            return fail("number out of range");
        }
        return true;
    }

    bool parse_value(JValue *out, int depth) {
        if (depth > 64)
            return fail("too deeply nested");
        skip_ws();
        if (pos >= text.size())
            return fail("unexpected end of input");
        char c = text[pos];
        if (c == '"') {
            out->type = JValue::STR;
            return parse_string(&out->s);
        }
        if (c == '{') {
            ++pos;
            out->type = JValue::OBJ;
            skip_ws();
            if (pos < text.size() && text[pos] == '}') {
                ++pos;
                return true;
            }
            while (true) {
                std::string key;
                skip_ws();
                if (!parse_string(&key))
                    return false;
                skip_ws();
                if (pos >= text.size() || text[pos] != ':')
                    return fail("expected ':'");
                ++pos;
                JValue v;
                if (!parse_value(&v, depth + 1))
                    return false;
                out->o.emplace_back(key, std::move(v));
                skip_ws();
                if (pos < text.size() && text[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (pos < text.size() && text[pos] == '}') {
                    ++pos;
                    return true;
                }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++pos;
            out->type = JValue::ARR;
            skip_ws();
            if (pos < text.size() && text[pos] == ']') {
                ++pos;
                return true;
            }
            while (true) {
                JValue v;
                if (!parse_value(&v, depth + 1))
                    return false;
                out->a.push_back(std::move(v));
                skip_ws();
                if (pos < text.size() && text[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (pos < text.size() && text[pos] == ']') {
                    ++pos;
                    return true;
                }
                return fail("expected ',' or ']'");
            }
        }
        if (text.compare(pos, 4, "true") == 0) {
            pos += 4;
            out->type = JValue::BOOL;
            out->b = true;
            return true;
        }
        if (text.compare(pos, 5, "false") == 0) {
            pos += 5;
            out->type = JValue::BOOL;
            out->b = false;
            return true;
        }
        if (text.compare(pos, 4, "null") == 0) {
            pos += 4;
            out->type = JValue::NUL;
            return true;
        }
        return parse_number(out);
    }
};

inline bool get_int(const JValue &obj, const char *key, int64_t *out,
             std::string *error, bool required) {
    const JValue *v = obj.get(key);
    if (!v) {
        if (required && error)
            *error = std::string("missing field: ") + key;
        return !required;
    }
    if (v->type != JValue::INT) {
        if (error)
            *error = std::string("non-integer field: ") + key;
        return false;
    }
    *out = v->i;
    return true;
}

inline bool get_string(const JValue &obj, const char *key, std::string *out,
                std::string *error, bool required) {
    const JValue *v = obj.get(key);
    if (!v) {
        if (required && error)
            *error = std::string("missing field: ") + key;
        return !required;
    }
    if (v->type != JValue::STR) {
        if (error)
            *error = std::string("non-string field: ") + key;
        return false;
    }
    *out = v->s;
    return true;
}


}  // namespace internal
}  // namespace pc

#endif /* PATTERN_CORE_JSON_INTERNAL_H */
