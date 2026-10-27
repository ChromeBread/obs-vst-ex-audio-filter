// Self-check for beat alignment: 4 kicks every 500 ms in the reference, 4 claps 87 ms late (with jitter
// and background noise) in the target must give a lag of about 87 ms. Exits non-zero on failure.
#include "dsp/beat-align.h"
#include "dsp/test-song.h"

#include <cstdio>
#include <cmath>
#include <cstdlib>

static void addHit(std::vector<float> &env, int at, float level, int decayMs)
{
	for (int i = 0; i < decayMs && at + i < int(env.size()); i++)
		env[at + i] = std::max(env[at + i], level * (1.0f - float(i) / decayMs));
}

int main()
{
	int failures = 0;
	std::srand(1);
	for (int lag : {0, 35, 87, 180, -40}) {
		std::vector<float> ref(5000, 0.02f), tgt(5000, 0.03f); // noise floor
		for (int b = 0; b < 8; b++)
			addHit(ref, 500 + b * 500, 0.9f, 120);
		for (int b = 0; b < 8; b++) // quiet arpeggio between the kicks must not count
			addHit(ref, 750 + b * 500, 0.2f, 60);
		const int jitter[] = {-6, 4, -2, 7};
		for (int b = 4; b < 8; b++)
			addHit(tgt, 500 + b * 500 + lag + jitter[b - 4], 0.7f, 40);
		auto res = vstex::alignOnsets(vstex::findOnsets(ref), vstex::findOnsets(tgt));
		bool ok = res.ok && std::abs(res.lagMs - lag) <= 8 && res.matched == 4;
		std::printf("lag %+4d ms: got %+4d ms, matched %d, spread %d  %s\n", lag, res.lagMs, res.matched, res.spreadMs,
			    ok ? "ok" : "FAIL");
		failures += !ok;
	}
	// Silence in the target must not produce a result.
	std::vector<float> ref(3000, 0.0f), quiet(3000, 0.0f);
	addHit(ref, 500, 1.0f, 100);
	bool silentOk = !vstex::alignOnsets(vstex::findOnsets(ref), vstex::findOnsets(quiet)).ok;
	std::printf("silent target: %s\n", silentOk ? "ok" : "FAIL");
	// The generated test song: every kick, and nothing else, must count as a beat (16 kicks, 500 ms apart).
	std::vector<float> song = vstex::makeTestSong(48000, 500, 8, 2);
	song.insert(song.begin(), 4800, 0.0f); // 100 ms of silence first, as the dialog records before the song starts
	std::vector<float> env(song.size() / 48 + 1, 0.0f);
	for (size_t i = 0; i < song.size(); i++)
		env[i / 48] = std::max(env[i / 48], std::fabs(song[i]));
	auto kicks = vstex::findOnsets(env);
	bool songOk = kicks.size() == 16;
	for (size_t i = 1; songOk && i < kicks.size(); i++)
		songOk = std::abs(kicks[i] - kicks[i - 1] - 500) <= 3;
	std::printf("test song: %zu kicks found  %s\n", kicks.size(), songOk ? "ok" : "FAIL");
	if (!songOk)
		for (int k : kicks)
			std::printf("  kick at %d ms\n", k);
	// Earphone held against the mic: the mic hears the same song, quieter, with room noise, 23 ms late.
	std::vector<float> mic(env.size(), 0.0f);
	for (size_t i = 23; i < env.size(); i++)
		mic[i] = 0.3f * env[i - 23] + 0.02f * float(std::rand() % 100) / 100.0f;
	auto ear = vstex::alignOnsets(kicks, vstex::findOnsets(mic));
	bool earOk = ear.ok && std::abs(ear.lagMs - 23) <= 2 && ear.matched >= 15;
	std::printf("earphone on mic: got %+d ms, matched %d  %s\n", ear.lagMs, ear.matched, earOk ? "ok" : "FAIL");
	return failures + !silentOk + !songOk + !earOk;
}
