#include "util.h"
#include "config.h"
#include "generated/build_info.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t parsePort(const char *str)
{
    if (!str || !*str)
        return 0;
    char *end = nullptr;
    unsigned long val = strtoul(str, &end, 10);
    if (*end != '\0' || val == 0 || val > 65535)
        return 0;
    return static_cast<uint16_t>(val);
}

bool parseMeterValue(const char *str, size_t len, uint32_t &result)
{
    // trim
    while (len > 0 && isspace(static_cast<unsigned char>(*str)))
    {
        str++;
        len--;
    }
    while (len > 0 && isspace(static_cast<unsigned char>(str[len - 1])))
        len--;
    if (len == 0)
        return false;

    uint32_t integerPart = 0;
    uint32_t fraction = 0;
    int fractionDigits = -1; // -1: no decimal separator seen yet
    for (size_t i = 0; i < len; i++)
    {
        char c = str[i];
        if ((c == '.' || c == ',') && fractionDigits < 0)
        {
            fractionDigits = 0;
        }
        else if (isdigit(static_cast<unsigned char>(c)))
        {
            if (fractionDigits < 0)
            {
                integerPart = integerPart * 10 + (c - '0');
                if (integerPart > MAX_METER_VALUE / 100)
                    return false;
            }
            else
            {
                if (++fractionDigits > 2)
                    return false;
                fraction = fraction * 10 + (c - '0');
            }
        }
        else
        {
            return false;
        }
    }
    if (fractionDigits == 1)
        fraction *= 10;
    result = integerPart * 100 + fraction;
    return result <= MAX_METER_VALUE;
}

const char *firmwareVersionLong()
{
    static char text[40];
    if (!text[0])
        snprintf(text, sizeof(text), "%s (%s%s)", FIRMWARE_VERSION, BUILD_COMMIT, BUILD_DIRTY ? "*" : "");
    return text;
}

void formatVolume(char *out, size_t size, uint32_t value)
{
    snprintf(out, size, "%lu.%02lu", (unsigned long)(value / 100), (unsigned long)(value % 100));
}

bool isValidTopic(const char *topic, size_t maxLen)
{
    size_t len = strlen(topic);
    if (len == 0 || len >= maxLen)
        return false;
    if (strpbrk(topic, "+#") || strstr(topic, "//"))
        return false;
    return topic[0] != '/' && topic[len - 1] != '/';
}
