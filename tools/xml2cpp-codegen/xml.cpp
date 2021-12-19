/**
 * Inspired by: http://dbus-cplusplus.sourceforge.net/
 */

#include "xml.h"
#include "generator_utils.h"

#include <expat.h>
#include <cstring>
#include <algorithm>

namespace {
    constexpr auto DefaultDocIndentation {"    "};
}

using namespace sdbuscpp::xml;

std::istream &operator >> (std::istream& in, Document& doc)
{
    std::stringbuf xmlbuf;
    in.get(xmlbuf, '\0');
    doc.from_xml(xmlbuf.str());

    return in;
}

std::ostream &operator << (std::ostream& out, const Document& doc)
{
    return out << doc.to_xml();
}


Error::Error(const char *error, int line, int column)
{
    std::ostringstream estream;

    estream << "line " << line << ", column " << column << ": " << error;

    m_error = estream.str();
}

Node::Node(const char *n, const char **a)
: name(n)
{
    if (a)
    {
        for (int i = 0; a[i]; i += 2)
        {
            m_attrs[a[i]] = a[i + 1];
        }
    }
}

Nodes Nodes::operator[](const std::string& key) const
{
    Nodes result;

    for (auto it = begin(), endIt = end(); it != endIt; ++it)
    {
        Nodes part = (**it)[key];

        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}

Nodes Nodes::select(const std::string& attr, const std::string& value) const
{
    Nodes result;

    for (auto it = begin(), itEnd = end(); it != itEnd; ++it)
    {
        if ((*it)->get(attr) == value)
        {
            result.insert(result.end(), *it);
        }
    }
    return result;
}

Nodes Node::operator[](const std::string& key)
{
    Nodes result;

    if (key.length() == 0)
    {
        return result;
    }

    for (auto it = children.begin(), itEnd = children.end(); it != itEnd; ++it)
    {
        if (it->name == key)
        {
            result.push_back(&(*it));
        }
    }
    return result;
}

std::string Node::get(const std::string& attribute) const
{
    const auto it = m_attrs.find(attribute);
    if (it != m_attrs.end())
    {
        return it->second;
    }

    return "";
}

void Node::set(const std::string& attribute, std::string value)
{
    if (value.length())
    {
        m_attrs[attribute] = value;
    }
    else
    {
        m_attrs.erase(value);
    }
}

std::string Node::to_xml() const
{
    std::string xml;
    int depth = 0;

    _raw_xml(xml, depth);

    return xml;
}

void Node::_raw_xml(std::string& xml, int& depth) const
{
    xml.append(depth * 2, ' ');
    xml.append("<" + name);

    for (auto it = m_attrs.begin(), itEnd = m_attrs.end(); it != itEnd; ++it)
    {
        xml.append(" " + it->first + "=\"" + it->second + "\"");
    }

    if (cdata.length() == 0 && children.size() == 0)
    {
        xml.append("/>\n");
    }
    else
    {
        xml.append(">");

        if (cdata.length())
        {
            xml.append(cdata);
        }

        if (children.size())
        {
            xml.append("\n");
            ++depth;

            for (auto it = children.begin(), itEnd = children.end(); it != itEnd; ++it)
            {
                it->_raw_xml(xml, depth);
            }

            --depth;
            xml.append(depth * 2, ' ');
        }

        xml.append("</" + name + ">\n");
    }
}

Document::Document(bool copyDoxygen) :
        root(nullptr),
        m_copyDoxygen(copyDoxygen),
        m_depth(0)
{
}

Document::Document(const std::string &xml, bool copyDoxygen) :
        root(nullptr),
        m_copyDoxygen(copyDoxygen),
        m_depth(0)
{
    from_xml(xml);
}

Document::~Document()
{
    delete root;
}

struct Document::Expat
{
        static void start_doctype_decl_handler(
                void *data, const XML_Char *name, const XML_Char *sysid, const XML_Char *pubid, int has_internal_subset
        );
        static void end_doctype_decl_handler(void *data);
        static void start_element_handler(void *data, const XML_Char *name, const XML_Char **atts);
        static void character_data_handler(void *data, const XML_Char *chars, int len);
        static void end_element_handler(void *data, const XML_Char *name);

        static void start_comment_decl_handler(void *data, const XML_Char *name);
};

void Document::from_xml(const std::string& xml)
{
    m_depth = 0;
    if (root)
    {
        delete root;
    }
    root = nullptr;

    XML_Parser parser = XML_ParserCreate("UTF-8");

    XML_SetUserData(parser, this);

    XML_SetDoctypeDeclHandler(
            parser,
            Document::Expat::start_doctype_decl_handler,
            Document::Expat::end_doctype_decl_handler
    );

    XML_SetElementHandler(
            parser,
            Document::Expat::start_element_handler,
            Document::Expat::end_element_handler
    );

    XML_SetCharacterDataHandler(
            parser,
            Document::Expat::character_data_handler
    );

    XML_SetCommentHandler(
            parser,
            Document::Expat::start_comment_decl_handler
            );

    XML_Status status = XML_Parse(parser, xml.c_str(), xml.length(), true);

    if (status == XML_STATUS_ERROR)
    {
        const char* error = XML_ErrorString(XML_GetErrorCode(parser));
        int line = XML_GetCurrentLineNumber(parser);
        int column = XML_GetCurrentColumnNumber(parser);

        XML_ParserFree(parser);

        throw Error(error, line, column);
    }
    else
    {
        XML_ParserFree(parser);
    }
}

std::string Document::to_xml() const
{
    return root->to_xml();
}

void Document::Expat::start_doctype_decl_handler(
        void* /*data*/,
        const XML_Char* /*name*/,
        const XML_Char* /*sysid*/,
        const XML_Char */*pubid*/,
        int /*has_internal_subset*/)
{
}

void Document::Expat::end_doctype_decl_handler(void* /*data*/)
{
}

void Document::Expat::start_element_handler(void* data, const XML_Char* name, const XML_Char** atts)
{
    Document* doc = static_cast<Document*>(data);

    if (!doc->root)
    {
        doc->root = new Node(name, atts);
    }
    else
    {
        Node::Children* cld = &(doc->root->children);

        for (int i = 1; i < doc->m_depth; ++i)
        {
            cld = &(cld->back().children);
        }
        Node n(name, atts);
        if (!cld->empty() && cld->back().name == "doc")
        {
            n.doc = std::move(cld->back().cdata);
            cld->pop_back();
        }

        cld->push_back(std::move(n));

    }
    doc->m_depth++;
}

void Document::Expat::character_data_handler(void* data, const XML_Char* chars, int len)
{
    Document* doc = static_cast<Document*>(data);

    Node* nod = doc->root;

    for (int i = 1; i < doc->m_depth; ++i)
    {
        nod = &(nod->children.back());
    }

    int x = 0, y = len - 1;

    while (isspace(chars[y]) && y > 0) --y;
    while (isspace(chars[x]) && x < y) ++x;

    nod->cdata = std::string(chars, x, y + 1);
}

void Document::Expat::end_element_handler(void* data, const XML_Char* /*name*/)
{
    Document* doc = static_cast<Document*>(data);

    doc->m_depth--;
}

void Document::Expat::start_comment_decl_handler(void *data, const XML_Char *comment)
{
    auto doc = static_cast<Document*>(data);

    if (!strstr(comment, "@brief") || !doc->m_copyDoxygen)
    {
        return;
    }

    if (!doc->root)
    {
        doc->root = new Node("doc");
        std::stringstream out;
        auto strstream = std::stringstream(comment);

        auto firstLineNotProcessed {true};
        for (std::string line ; std::getline(strstream, line);)
        {
            const auto firstCharacter = std::find_if(line.begin(), line.end(), [](unsigned char c) { return !std::isspace(c);});
            const auto offsetToVisibleCharacter = std::distance(line.begin(), firstCharacter);
            line.erase(line.begin(), firstCharacter);

            if (firstLineNotProcessed || strstream.eof() && line.empty()) {
                firstLineNotProcessed = false;
                continue;
            }

            if (!line.empty() && line[0] != '@')
            {
                out << DefaultDocIndentation;
            }

            out << line << std::endl;
        }
        doc->root->cdata = out.str();
    }
    else
    {
        Node::Children* cld = &(doc->root->children);

        for (int i = 1; i < doc->m_depth; ++i)
        {
            cld = &(cld->back().children);
        }
        Node documentation("doc");
        std::stringstream out;
        auto strstream = std::stringstream(comment);
        auto firstLineNotProcessed {true};
        for (std::string line ; std::getline(strstream, line);)
        {
            const auto firstCharacter = std::find_if(line.begin(), line.end(), [](unsigned char c) { return !std::isspace(c);});
            const auto offsetToVisibleCharacter = std::distance(line.begin(), firstCharacter);
            line.erase(line.begin(), firstCharacter);

            if (firstLineNotProcessed || strstream.eof() && line.empty()) {
                firstLineNotProcessed = false;
                continue;
            }

            if (!line.empty() && line[0] != '@')
            {
                out << DefaultDocIndentation;
            }

            out << line << std::endl;
        }
        documentation.cdata = out.str();
        cld->push_back(std::move(documentation));
    }
}

