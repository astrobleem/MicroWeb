#ifndef _FORM_ENCODING_H_
#define _FORM_ENCODING_H_
#include <stddef.h>

// A bounded application/x-www-form-urlencoded writer. Failed pairs are rolled
// back; callers must reject the entire submission rather than send partial data.
class FormEncoding
{
public:
    FormEncoding(char* out, size_t capacity) : buffer(out), size(capacity), used(0) { if (size && buffer) buffer[0] = 0; }
    bool Append(const char* name, const char* value)
    {
        if (!name || !*name) return true;
        size_t start = used;
        if ((used && !Put('&')) || !Encode(name) || !Put('=') || !Encode(value))
        {
            used = start;
            if (size && buffer) buffer[used] = 0;
            return false;
        }
        return true;
    }
private:
    bool Put(char c)
    {
        if (!size || used >= size - 1) return false;
        if (buffer) { buffer[used] = c; buffer[used + 1] = 0; }
        ++used; return true;
    }
    bool Byte(unsigned char c)
    {
        static const char hex[] = "0123456789ABCDEF";
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '*' || c == '-' || c == '.' || c == '_') return Put((char)c);
        if (c == ' ') return Put('+');
        return Put('%') && Put(hex[c >> 4]) && Put(hex[c & 15]);
    }
    bool Encode(const char* text)
    {
        if (!text) return true;
        while (*text)
        {
            unsigned char c = (unsigned char)*text++;
            // HTML form line breaks are CRLF, including textarea LF input.
            if (c == '\r' || c == '\n')
            {
                if (c == '\r' && *text == '\n') ++text;
                if (!Byte('\r') || !Byte('\n')) return false;
            }
            else if (!Byte(c)) return false;
        }
        return true;
    }
    char* buffer;
    size_t size, used;
};
#endif
