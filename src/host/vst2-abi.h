#pragma once

// Minimal VST 2.4 binary interface, written from the publicly documented ABI for interoperability.
// The Steinberg VST2 SDK itself is not redistributable and is not used.

#include <cstdint>

#define VST2_CALL __cdecl

namespace vst2 {

struct AEffect;

using HostCallback = intptr_t(VST2_CALL *)(AEffect *effect, int32_t opcode, int32_t index, intptr_t value, void *ptr,
					   float opt);
using DispatcherProc = intptr_t(VST2_CALL *)(AEffect *effect, int32_t opcode, int32_t index, intptr_t value,
					     void *ptr, float opt);
using ProcessProc = void(VST2_CALL *)(AEffect *effect, float **inputs, float **outputs, int32_t frames);
using SetParameterProc = void(VST2_CALL *)(AEffect *effect, int32_t index, float value);
using GetParameterProc = float(VST2_CALL *)(AEffect *effect, int32_t index);
using EntryProc = AEffect *(VST2_CALL *)(HostCallback host);

constexpr int32_t kEffectMagic = ('V' << 24) | ('s' << 16) | ('t' << 8) | 'P';

struct AEffect {
	int32_t magic;
	DispatcherProc dispatcher;
	ProcessProc process; // deprecated accumulating process
	SetParameterProc setParameter;
	GetParameterProc getParameter;
	int32_t numPrograms;
	int32_t numParams;
	int32_t numInputs;
	int32_t numOutputs;
	int32_t flags;
	intptr_t resvd1; // reserved for the host: we store our instance pointer here
	intptr_t resvd2;
	int32_t initialDelay; // latency in samples
	int32_t realQualities;
	int32_t offQualities;
	float ioRatio;
	void *object;
	void *user;
	int32_t uniqueID;
	int32_t version;
	ProcessProc processReplacing;
	void *processDoubleReplacing;
	char future[56];
};

struct ERect {
	int16_t top, left, bottom, right;
};

enum EffectFlags : int32_t {
	effFlagsHasEditor = 1 << 0,
	effFlagsCanReplacing = 1 << 4,
	effFlagsProgramChunks = 1 << 5,
	effFlagsIsSynth = 1 << 8,
};

enum EffectOpcodes : int32_t {
	effOpen = 0,
	effClose = 1,
	effGetParamName = 8,
	effSetSampleRate = 10,
	effSetBlockSize = 11,
	effMainsChanged = 12,
	effEditGetRect = 13,
	effEditOpen = 14,
	effEditClose = 15,
	effEditIdle = 19,
	effGetChunk = 23,
	effSetChunk = 24,
	effGetPlugCategory = 35,
	effGetEffectName = 45,
	effGetVendorString = 47,
	effGetProductString = 48,
	effGetVendorVersion = 49,
	effStartProcess = 71,
	effStopProcess = 72,
};

enum HostOpcodes : int32_t {
	audioMasterAutomate = 0,
	audioMasterVersion = 1,
	audioMasterCurrentId = 2,
	audioMasterIdle = 3,
	audioMasterGetTime = 7,
	audioMasterIOChanged = 13,
	audioMasterSizeWindow = 15,
	audioMasterGetSampleRate = 16,
	audioMasterGetBlockSize = 17,
	audioMasterGetCurrentProcessLevel = 23,
	audioMasterGetVendorString = 32,
	audioMasterGetProductString = 33,
	audioMasterGetVendorVersion = 34,
	audioMasterCanDo = 37,
	audioMasterGetLanguage = 38,
	audioMasterUpdateDisplay = 42,
	audioMasterBeginEdit = 43,
	audioMasterEndEdit = 44,
};

enum PlugCategory : int32_t {
	kPlugCategUnknown = 0,
	kPlugCategEffect,
	kPlugCategSynth,
	kPlugCategAnalysis,
	kPlugCategMastering,
	kPlugCategSpacializer,
	kPlugCategRoomFx,
	kPlugSurroundFx,
	kPlugCategRestoration,
	kPlugCategOfflineProcess,
	kPlugCategShell,
	kPlugCategGenerator,
};

} // namespace vst2
