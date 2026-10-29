#pragma once

// The few JUCE storage formats atkAudio's plugin host uses, written out so we can read and write its
// filter settings without JUCE (see juce_MemoryBlock.cpp, juce_AudioProcessor.cpp, juce_VST3PluginFormatImpl.h).

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace jucefmt {

// MemoryBlock::toBase64Encoding: "<size>." then 6-bit groups, least significant bits first, in JUCE's own alphabet.
inline std::string base64Encode(const std::vector<uint8_t> &data)
{
	static const char table[] = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+";
	std::string out = std::to_string(data.size()) + ".";
	size_t chars = (data.size() * 8 + 5) / 6;
	for (size_t i = 0; i < chars; i++) {
		size_t bit = i * 6;
		int value = 0;
		for (int b = 0; b < 6 && (bit + b) / 8 < data.size(); b++)
			value |= ((data[(bit + b) / 8] >> ((bit + b) % 8)) & 1) << b;
		out += table[value];
	}
	return out;
}

// MemoryBlock::fromBase64Encoding. False if there is no "<size>." prefix.
inline bool base64Decode(const std::string &text, std::vector<uint8_t> &out)
{
	static const signed char table[] = {63, 0,  0,  0,  0,  53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 0,  0,  0,  0,
					    0,  0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
					    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 0,  0,  0,  0,  0,  0,  27, 28, 29,
					    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
					    49, 50, 51, 52};
	size_t dot = text.find('.');
	if (dot == std::string::npos || dot == 0)
		return false;
	out.assign(std::strtoull(text.substr(0, dot).c_str(), nullptr, 10), 0);
	size_t pos = 0;
	for (size_t i = dot + 1; i < text.size(); i++) {
		int c = (unsigned char)text[i] - 43;
		if (c < 0 || c >= (int)sizeof(table))
			continue; // JUCE skips anything outside the alphabet (whitespace, line breaks)
		int value = table[c];
		for (int b = 0; b < 6; b++) {
			size_t bit = pos * 6 + b;
			if (bit / 8 >= out.size())
				break;
			if (value & (1 << b))
				out[bit / 8] |= uint8_t(1u << (bit % 8));
		}
		pos++;
	}
	return true;
}

// AudioProcessor::copyXmlToBinary: magic "VC2!", length, UTF-8 XML, terminating zero.
inline std::vector<uint8_t> xmlToBinary(const std::string &xml)
{
	std::vector<uint8_t> out(8);
	uint32_t magic = 0x21324356, length = uint32_t(xml.size() + 1);
	std::memcpy(out.data(), &magic, 4);
	std::memcpy(out.data() + 4, &length, 4);
	out.insert(out.end(), xml.begin(), xml.end());
	out.push_back(0);
	return out;
}

// AudioProcessor::getXmlFromBinary. Empty if the data is not in that format.
inline std::string binaryToXml(const std::vector<uint8_t> &data)
{
	uint32_t magic = 0, length = 0;
	if (data.size() <= 8)
		return {};
	std::memcpy(&magic, data.data(), 4);
	std::memcpy(&length, data.data() + 4, 4);
	if (magic != 0x21324356 || length == 0)
		return {};
	std::string xml((const char *)data.data() + 8, std::min<size_t>(length, data.size() - 8));
	if (!xml.empty() && xml.back() == 0)
		xml.pop_back();
	return xml;
}

// getHashForRange: how JUCE turns a VST3 class id into PluginDescription::uniqueId / deprecatedUid.
template<typename T, size_t N> int32_t hashRange(const T (&items)[N])
{
	uint32_t value = 0;
	for (const T &item : items)
		value = value * 31 + (uint32_t)item; // a signed char sign-extends, as in JUCE
	return (int32_t)value;
}

} // namespace jucefmt
