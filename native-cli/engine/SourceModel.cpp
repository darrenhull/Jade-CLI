#include "SourceModel.hpp"
#include <algorithm>
#include <cctype>
#include <set>
namespace jade::native {
namespace {
struct Token {
    std::string value;
    size_t offset, line;
};
bool identifierStart(unsigned char c) {
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool identifierChar(unsigned char c) {
    return identifierStart(c) || (c >= '0' && c <= '9');
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
} // namespace
SourceModel SourceModel::parse(const std::string &text, const std::string &source) {
    SourceModel m;
    m.text = text;
    m.source = source;
    m.code = text;
    enum State {
        Plain,
        Single,
        Double,
        Backtick,
        LineComment,
        BlockComment
    };
    State state = Plain;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i], next = i + 1 < text.size() ? text[i + 1] : 0;
        if (state == Plain) {
            if (c == '/' && next == '/') {
                state = LineComment;
                m.code[i] = ' ';
            } else if (c == '/' && next == '*') {
                state = BlockComment;
                m.code[i] = ' ';
            } else if (c == '\'' || c == '"' || c == '`') {
                state = c == '\'' ? Single : c == '"' ? Double : Backtick;
                m.code[i] = ' ';
            }
        } else {
            if (c != '\n' && c != '\r')
                m.code[i] = ' ';
            if (state == LineComment && c == '\n')
                state = Plain;
            else if (state == BlockComment && c == '*' && next == '/') {
                m.code[++i] = ' ';
                state = Plain;
            } else if ((state == Single && c == '\'') || (state == Double && c == '"') ||
                       (state == Backtick && c == '`')) {
                if (next == c)
                    m.code[++i] = ' ';
                else
                    state = Plain;
            }
        }
    }
    std::vector<Token> tokens;
    size_t line = 1;
    for (size_t i = 0; i < m.code.size();) {
        unsigned char c = m.code[i];
        if (c == '\n') {
            ++line;
            ++i;
        } else if (std::isspace(c)) {
            ++i;
        } else if (identifierStart(c)) {
            auto start = i++;
            while (i < m.code.size() && identifierChar(static_cast<unsigned char>(m.code[i])))
                ++i;
            tokens.push_back({m.code.substr(start, i - start), start, line});
        } else {
            tokens.push_back({m.code.substr(i, 1), i, line});
            ++i;
        }
    }
    std::set<std::string> identifiers;
    for (auto &token : tokens)
        if (identifierStart(token.value[0]))
            identifiers.insert(token.value);
    m.identifiers.assign(identifiers.begin(), identifiers.end());
    size_t headers = tokens.size(), definitions = tokens.size(), sources = tokens.size();
    for (size_t i = 0; i < tokens.size(); ++i) {
        auto &t = tokens[i].value;
        if (t == "schemaDefinition" && i + 4 < tokens.size() &&
            tokens[i + 2].value == "subschemaOf") {
            m.schema = tokens[i + 1].value;
            m.parentSchema = tokens[i + 3].value;
        } else if (t == "typeHeaders" && headers == tokens.size())
            headers = i;
        else if (t == "typeDefinitions" && definitions == tokens.size())
            definitions = i;
        else if (t == "typeSources" && sources == tokens.size())
            sources = i;
    }
    if (headers < tokens.size())
        for (size_t i = headers + 1; i + 2 < std::min(definitions, tokens.size()); ++i)
            if (tokens[i + 1].value == "subclassOf" && identifierStart(tokens[i].value[0])) {
                ClassDeclaration c{tokens[i].value, tokens[i + 2].value, false};
                size_t end = i + 3;
                for (; end < definitions && tokens[end].value != ";"; ++end)
                    if (tokens[end].value == "partial" || tokens[end].value == "partialDefinition")
                        c.partial = true;
                if (end < definitions) {
                    m.classes.push_back(std::move(c));
                    i = end;
                }
            }
    if (sources < tokens.size())
        for (size_t i = sources + 1; i + 3 < tokens.size(); ++i)
            if (identifierStart(tokens[i].value[0]) && tokens[i + 1].value == "{" &&
                tokens[i + 2].value == tokens[i].value && tokens[i + 3].value == "(") {
                size_t end = i + 4;
                unsigned depth = 1;
                for (; end < tokens.size() && depth; ++end) {
                    if (tokens[end].value == "(")
                        ++depth;
                    else if (tokens[end].value == ")")
                        --depth;
                    else if (tokens[end].value == "{" || tokens[end].value == "}")
                        break;
                }
                if (depth)
                    continue;
                size_t flagsEnd = end;
                for (; flagsEnd < tokens.size(); ++flagsEnd) {
                    auto &value = tokens[flagsEnd].value;
                    if (value == ";" || value == "begin" || value == "vars" || value == "epilog" ||
                        value == "{" || value == "}")
                        break;
                }
                if (flagsEnd == tokens.size() || tokens[flagsEnd].value != ";")
                    continue;
                bool unit = false, ignored = false;
                for (size_t flag = end; flag < flagsEnd; ++flag) {
                    auto value = lower(tokens[flag].value);
                    if (value == "unittest")
                        unit = true;
                    if (value == "unittestignore")
                        ignored = true;
                }
                m.methods.push_back(
                    {tokens[i].value,
                     m.code.substr(tokens[i + 3].offset,
                                   tokens[flagsEnd].offset - tokens[i + 3].offset + 1),
                     tokens[i].line, tokens[i + 1].line, unit, ignored});
                i = flagsEnd;
            }
    return m;
}
} // namespace jade::native
