#pragma once

#include <cmath>
#include <vector>

namespace vstex {

// KeyGen-style test song, two 8-beat loops. Loop 1: square arpeggio in 16ths, bass on the off-beats.
// Loop 2: a 32nd-note pulse-wave arpeggio over Am-F-C-G with a dotted echo, bouncing octave bass and hats.
// The kick is up front and everything else stays low, so the beat is what both sides pick up, even with
// an earphone held right against the mic. Each kick starts with a 1.5 ms click: a sharp edge times better
// than the kick's soft sine.
inline std::vector<float> makeTestSong(int sampleRate, int beatMs, int loopBeats, int loops)
{
	const int songBeats = loopBeats * loops;
	const double pi = 3.14159265358979323846;
	const int beatSamples = sampleRate * beatMs / 1000;
	const int total = beatSamples * songBeats + sampleRate / 2;
	std::vector<float> mix(total, 0.0f);
	auto add = [&](int at, float v) {
		if (at >= 0 && at < total)
			mix[at] += v;
	};
	auto pulse = [&](double f, double t, double duty) { return std::fmod(f * t, 1.0) < duty ? 1.0 : -1.0; };

	const double arp1[] = {440.00, 523.25, 659.25, 880.00, 659.25, 523.25, 440.00, 659.25}; // A minor
	const double bass1[] = {110.00, 110.00, 87.31, 98.00};
	// Loop 2 chords, two beats each: Am F C G (root, third, fifth)
	const double chords[4][3] = {{220.00, 261.63, 329.63}, {174.61, 220.00, 261.63}, {261.63, 329.63, 392.00},
				     {196.00, 246.94, 293.66}};
	const int climb[8] = {0, 1, 2, 3, 4, 5, 4, 3}; // up two octaves and back
	const double melody = 0.35; // level of everything but the kick
	unsigned clickNoise = 777u;

	for (int beat = 0; beat < songBeats; beat++) {
		int at = beat * beatSamples;
		for (int i = 0; i < sampleRate / 8; i++) { // kick: falling sine
			double t = double(i) / sampleRate;
			double f = 50.0 + 110.0 * std::exp(-t * 30.0);
			add(at + i, float(0.8 * std::exp(-t * 18.0) * std::sin(2 * pi * f * t)));
		}
		for (int i = 0; i < sampleRate * 3 / 2000; i++) { // click on the attack
			clickNoise = clickNoise * 1664525u + 1013904223u;
			add(at + i, float(0.45 * ((clickNoise >> 16) / 32768.0 - 1.0) * (1.0 - double(i) / (sampleRate * 3 / 2000))));
		}

		if (beat < loopBeats) {
			for (int step = 0; step < 4; step++) { // 16th-note arpeggio
				int start = at + step * beatSamples / 4;
				double f = arp1[(beat * 4 + step) % 8];
				for (int i = 0; i < beatSamples / 4 - 200; i++) {
					double t = double(i) / sampleRate;
					add(start + i, float(melody * 0.10 * pulse(f, t, 0.5) * std::exp(-t * 9.0)));
				}
			}
			int off = at + beatSamples / 2; // bass on the off-beat
			for (int i = 0; i < beatSamples / 3; i++) {
				double t = double(i) / sampleRate;
				double tri = 2.0 / pi * std::asin(std::sin(2 * pi * bass1[beat % 4] * t));
				add(off + i, float(melody * 0.15 * tri * std::exp(-t * 6.0)));
			}
			continue;
		}

		const double *chord = chords[((beat - loopBeats) / 2) % 4];
		double tones[6] = {chord[0] * 2, chord[1] * 2, chord[2] * 2, chord[0] * 4, chord[1] * 4, chord[2] * 4};
		double duty = beat % 2 ? 0.125 : 0.25; // thin/fat pulse, alternating per beat
		const int echo = beatSamples * 3 / 8;   // dotted-16th echo
		for (int step = 0; step < 8; step++) { // 32nd notes
			int start = at + step * beatSamples / 8;
			double f = tones[climb[step]];
			for (int i = 0; i < beatSamples / 8 - 60; i++) {
				double t = double(i) / sampleRate;
				double v = pulse(f, t, duty) * std::exp(-t * 14.0);
				add(start + i, float(melody * 0.09 * v));
				add(start + echo + i, float(melody * 0.035 * v));
			}
		}
		for (int half = 0; half < 2; half++) { // bass: root, then its octave on the off-beat
			int start = at + half * beatSamples / 2;
			double f = chord[0] / 2 * (half ? 2 : 1);
			for (int i = 0; i < beatSamples / 2 - 400; i++) {
				double t = double(i) / sampleRate;
				add(start + i, float(melody * 0.12 * pulse(f, t, 0.5) * std::exp(-t * 5.0)));
			}
		}
		unsigned noise = 12345u + unsigned(beat);
		int hat = at + beatSamples / 2; // closed hat on the off-beat
		for (int i = 0; i < sampleRate / 40; i++) {
			noise = noise * 1664525u + 1013904223u;
			double t = double(i) / sampleRate;
			add(hat + i, float(melody * 0.05 * ((noise >> 16) / 32768.0 - 1.0) * std::exp(-t * 120.0)));
		}
	}

	return mix;
}

} // namespace vstex
