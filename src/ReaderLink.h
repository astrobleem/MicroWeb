#ifndef _READER_LINK_H_
#define _READER_LINK_H_

#include <stddef.h>
#include <string.h>

// Reader services accept the destination as a query parameter. Keep the
// service configurable: a public service disappearing must not require a build.
class ReaderLink
{
public:
    enum Result { Ready, NotConfigured, InvalidPrefix, InvalidTarget, TooLong };

    // The buffers must not overlap. On failure, output is an empty string.
    static Result Build(char* output, size_t capacity, const char* prefix, const char* target)
    {
        if (output && capacity) output[0] = 0;
        if (!prefix || !*prefix) return NotConfigured;
        if (!HasScheme(prefix, "http://")) return InvalidPrefix;
        const char* authority = prefix + 7;
        const char* query = strchr(authority, '?');
        const char* slash = strchr(authority, '/');
        if (!query || !slash || slash == authority || slash > query ||
            query[1] == 0) return InvalidPrefix;
        for (const char* p = authority; p < slash; ++p)
            if (*p == '@') return InvalidPrefix;
        const char* last = prefix;
        for (const char* p = prefix; *p; ++p)
        {
            unsigned char c = (unsigned char)*p;
            if (c <= 32 || c >= 127 || c == '"' || c == '\'' ||
                c == '<' || c == '>' || c == '#' || c == '\\') return InvalidPrefix;
            last = p;
        }
        // The last query field must have a name and an empty value.
        const char* field = strrchr(query, '&');
        field = field ? field + 1 : query + 1;
        if (*last != '=' || field == last || strchr(field, '=') != last) return InvalidPrefix;
        if (!target || !HasScheme(target, "https://") || !target[8]) return InvalidTarget;
        for (const char* p = target; *p; ++p)
            if ((unsigned char)*p < 32 || (unsigned char)*p == 127) return InvalidTarget;
        if (!output || !capacity) return TooLong;

        size_t used = 0;
        for (const char* p = prefix; *p; ++p)
            if (!Put(output, capacity, used, *p)) return TooLong;
        static const char hex[] = "0123456789ABCDEF";
        for (const char* p = target; *p; ++p)
        {
            unsigned char c = (unsigned char)*p;
            bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
            if (plain)
            {
                if (!Put(output, capacity, used, (char)c)) return TooLong;
            }
            else if (!Put(output, capacity, used, '%') ||
                     !Put(output, capacity, used, hex[c >> 4]) ||
                     !Put(output, capacity, used, hex[c & 15])) return TooLong;
        }
        return Ready;
    }

    // Use the same output path with HTMLParser in the app and a capture sink
    // in the host tests. Dynamic text is always HTML-escaped.
    template<class Writer>
    static void WritePage(Writer& writer, Result result, const char* readerURL)
    {
        writer.Write("<html><h1>HTTPS unsupported</h1><hr>");
        writer.Write("No HTTPS gateway is configured. This browser cannot connect directly to HTTPS sites.<br>");
        if (result == Ready)
        {
            writer.Write("<a href=\"");
            for (const char* p = readerURL; *p; ++p)
            {
                if (*p == '&') writer.Write("&amp;");
                else { char c[2] = { *p, 0 }; writer.Write(c); }
            }
            writer.Write("\">Visit using your configured reader</a><br>");
            writer.Write("Following this link sends the full address to that reader over unencrypted HTTP. Do not use it for private links.<br>");
        }
        else if (result == NotConfigured)
            writer.Write("No reader service is configured. Set MICROWEB_READER to a trusted HTTP reader URL prefix before starting MicroWeb.<br>");
        else if (result == InvalidPrefix)
            writer.Write("MICROWEB_READER must be an HTTP URL ending in an empty query field, for example http://your-reader/read.php?a=<br>");
        else if (result == TooLong)
            writer.Write("This address is too long for the configured reader link. It has not been truncated or sent.<br>");
        else
            writer.Write("This HTTPS address cannot be passed to the reader.<br>");
        writer.Write("Alternatively, set MICROWEB_HTTPS_PROXY to a trusted HTTPS-to-HTTP gateway. The DOS-to-gateway link is unencrypted. See README.md for setup.<br>");
        writer.Write("</html>");
    }

private:
    static bool HasScheme(const char* text, const char* scheme)
    {
        for (; *scheme; ++scheme, ++text)
        {
            char c = *text;
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (c != *scheme) return false;
        }
        return true;
    }
    static bool Put(char* output, size_t capacity, size_t& used, char c)
    {
        if (used >= capacity - 1) { output[0] = 0; return false; }
        output[used++] = c;
        output[used] = 0;
        return true;
    }
};

#endif
