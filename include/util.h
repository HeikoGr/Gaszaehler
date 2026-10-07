// Parsing and formatting helpers without Arduino dependencies
#pragma once
#include <stddef.h>
#include <stdint.h>

// Parses a TCP port, returns 0 if invalid
uint16_t parsePort(const char *str);

// Parses a meter value in m3 ("1234.56" or "1234,56", no thousands separators, surrounding
// whitespace allowed) into 1/100 m3. Returns false on invalid or out of range input.
bool parseMeterValue(const char *str, size_t len, uint32_t &result);

// Formats a volume in 1/100 m3 as "12345.67"
void formatVolume(char *out, size_t size, uint32_t value);

// MQTT topic level(s) entered by the user: no wildcards, no empty levels, shorter than maxLen
bool isValidTopic(const char *topic, size_t maxLen);
