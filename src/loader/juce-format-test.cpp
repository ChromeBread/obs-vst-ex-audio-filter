// Self-check for juce-format.h: `cmake --build ... --target juce-format-test` then run it (non-zero exit = failure).

#include "loader/juce-format.h"

#include <cstdio>

static int failures = 0;

static void check(bool ok, const char *what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

int main()
{
	// Hand-computed from JUCE's bit order and alphabet.
	check(jucefmt::base64Encode(std::vector<uint8_t>(1, 0)) == "1...", "encode 0x00"); // size, dot, 2 zero groups
	check(jucefmt::base64Encode({0xFF}) == "1.+C", "encode 0xFF");
	check(jucefmt::base64Encode({}) == "0.", "encode empty");

	std::vector<uint8_t> all;
	for (int i = 0; i < 256; i++)
		all.push_back(uint8_t(i * 37 + 11));
	for (size_t n = 0; n <= all.size(); n += 7) {
		std::vector<uint8_t> part(all.begin(), all.begin() + n), back;
		check(jucefmt::base64Decode(jucefmt::base64Encode(part), back) && back == part, "base64 round trip");
	}
	std::vector<uint8_t> out;
	check(!jucefmt::base64Decode("no dot", out), "reject text without size");

	std::string xml = "<VST3PluginState><IComponent>4.ABCD</IComponent></VST3PluginState>";
	check(jucefmt::binaryToXml(jucefmt::xmlToBinary(xml)) == xml, "xml binary round trip");
	check(jucefmt::binaryToXml({1, 2, 3, 4, 5, 6, 7, 8, 9}).empty(), "reject non-JUCE binary");

	const char signedBytes[2] = {1, -1};
	check(jucefmt::hashRange(signedBytes) == int32_t(31u * 1u + 0xFFFFFFFFu), "hash sign-extends chars");

	std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
	return failures ? 1 : 0;
}
