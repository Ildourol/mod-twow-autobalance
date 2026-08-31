/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1)
 */

#ifndef MOD_TWOW_AUTOBALANCE_STRING_FORMAT_H
#define MOD_TWOW_AUTOBALANCE_STRING_FORMAT_H

#include <iomanip>
#include <string>
#include <sstream>

// Keep the compatibility formatter private to this module. Other modules may
// provide their own Acore::StringFormat from a force-included compatibility
// header, and two template definitions with that same name are ill-formed in
// a single translation unit.
namespace AutoBalanceFormatting
{
    template<class A>
    inline void AbStreamValue(std::ostringstream& stream, A const& value)
    {
        stream << value;
    }

    inline void AbStreamValue(std::ostringstream& stream, unsigned char value)
    {
        stream << static_cast<unsigned int>(value);
    }

    inline void AbStreamValue(std::ostringstream& stream, signed char value)
    {
        stream << static_cast<int>(value);
    }

    inline int AbFixedPrecision(char const* begin, char const* end)
    {
        if (end - begin < 4 || begin[0] != ':' || begin[1] != '.' || end[-1] != 'f')
            return -1;

        int precision = 0;
        for (char const* current = begin + 2; current < end - 1; ++current)
        {
            if (*current < '0' || *current > '9')
                return -1;
            precision = precision * 10 + (*current - '0');
        }
        return precision;
    }

    inline void AbFormatStep(std::string& out, char const*& p)
    {
        out += p;
        p += std::string(p).size();
    }

    template<class A, class... Rest>
    inline void AbFormatStep(std::string& out, char const*& p, A const& a, Rest const&... rest)
    {
        while (*p)
        {
            if (p[0] == '{')
            {
                char const* close = p + 1;
                while (*close && *close != '}')
                    ++close;

                if (*close == '}')
                {
                    std::ostringstream os;
                    int const precision = AbFixedPrecision(p + 1, close);
                    if (precision >= 0)
                        os << std::fixed << std::setprecision(precision);
                    AbStreamValue(os, a);
                    out += os.str();
                    p = close + 1;
                    AbFormatStep(out, p, rest...);
                    return;
                }
            }
            out += *p++;
        }
    }

    template<class... Args>
    inline std::string StringFormat(char const* fmt, Args const&... args)
    {
        std::string out;
        char const* p = fmt;
        AbFormatStep(out, p, args...);
        return out;
    }

    template<class... Args>
    inline std::string StringFormat(std::string const& fmt, Args const&... args)
    {
        return StringFormat(fmt.c_str(), args...);
    }
}

#endif // MOD_TWOW_AUTOBALANCE_STRING_FORMAT_H
