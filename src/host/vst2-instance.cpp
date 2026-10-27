#include "host/plugin-host.h"
#include "host/vst2-abi.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

using namespace vst2;

namespace vstex {

namespace {

std::wstring widen(const std::string &s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
	std::wstring w(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
	return w;
}

std::string fileStem(const std::string &path)
{
	size_t slash = path.find_last_of("/\\");
	std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
	size_t dot = name.find_last_of('.');
	return dot == std::string::npos ? name : name.substr(0, dot);
}

const char *categoryName(intptr_t category, bool isSynth)
{
	switch (category) {
	case kPlugCategEffect:
		return "Effect";
	case kPlugCategSynth:
		return "Instrument";
	case kPlugCategAnalysis:
		return "Analyzer";
	case kPlugCategMastering:
		return "Mastering";
	case kPlugCategSpacializer:
		return "Spatial";
	case kPlugCategRoomFx:
		return "Reverb";
	case kPlugSurroundFx:
		return "Surround";
	case kPlugCategRestoration:
		return "Restoration";
	case kPlugCategOfflineProcess:
		return "Offline";
	case kPlugCategShell:
		return "Shell";
	case kPlugCategGenerator:
		return "Generator";
	default:
		return isSynth ? "Instrument" : "Effect";
	}
}

class Vst2Instance;
thread_local Vst2Instance *tlsLoading = nullptr;

intptr_t VST2_CALL hostCallback(AEffect *effect, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt);

class Vst2Instance final : public PluginInstance {
public:
	~Vst2Instance() override
	{
		if (effect) {
			closeEditor();
			if (prepared) {
				dispatch(effStopProcess);
				dispatch(effMainsChanged, 0, 0);
			}
			dispatch(effClose);
		}
		if (module)
			FreeLibrary(module);
	}

	bool load(const std::string &path, std::string &error)
	{
		module = LoadLibraryExW(widen(path).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!module) {
			error = "LoadLibrary failed (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		auto entry = (EntryProc)GetProcAddress(module, "VSTPluginMain");
		if (!entry)
			entry = (EntryProc)GetProcAddress(module, "main");
		if (!entry) {
			error = "not a VST2 plugin (no VSTPluginMain export)";
			return false;
		}

		tlsLoading = this;
		effect = entry(hostCallback);
		tlsLoading = nullptr;
		if (!effect || effect->magic != kEffectMagic) {
			effect = nullptr;
			error = "VST2 entry point returned no effect";
			return false;
		}
		effect->resvd1 = (intptr_t)this;
		dispatch(effOpen);

		desc.format = PluginFormat::VST2;
		desc.path = path;
		desc.uid = std::to_string(effect->uniqueID);
		desc.name = getString(effGetEffectName);
		if (desc.name.empty())
			desc.name = fileStem(path);
		desc.vendor = getString(effGetVendorString);
		desc.description = getString(effGetProductString);
		// Conventionally 0xMMmmpp, e.g. 0x020106 = 2.1.6
		intptr_t ver = dispatch(effGetVendorVersion);
		if (!ver)
			ver = effect->version;
		desc.version = std::to_string((ver >> 16) & 0xff) + "." + std::to_string((ver >> 8) & 0xff) + "." +
			       std::to_string(ver & 0xff);
		desc.category =
			categoryName(dispatch(effGetPlugCategory), (effect->flags & effFlagsIsSynth) != 0);
		desc.numInputs = effect->numInputs;
		desc.numOutputs = effect->numOutputs;
		desc.hasEditor = (effect->flags & effFlagsHasEditor) != 0;
		return true;
	}

	const PluginDescription &description() const override { return desc; }

	bool prepare(double sr, int maxBlock, int /*numChannels*/) override
	{
		sampleRate = sr;
		blockSize = maxBlock;
		if (prepared) {
			dispatch(effStopProcess);
			dispatch(effMainsChanged, 0, 0);
		}
		dispatch(effSetSampleRate, 0, 0, nullptr, (float)sr);
		dispatch(effSetBlockSize, 0, maxBlock);
		dispatch(effMainsChanged, 0, 1);
		dispatch(effStartProcess);
		mapper.resize(effect->numInputs, effect->numOutputs, maxBlock);
		prepared = true;
		return true;
	}

	void process(float *const *channels, int numChannels, int frames) override
	{
		if (!prepared || !(effect->flags & effFlagsCanReplacing))
			return;
		for (int offset = 0; offset < frames; offset += blockSize) {
			int n = std::min(blockSize, frames - offset);
			mapper.pull(channels, numChannels, offset, n);
			effect->processReplacing(effect, mapper.inputs(), mapper.outputs(), n);
			mapper.push(channels, numChannels, offset, n);
		}
	}

	int latencySamples() override { return effect->initialDelay; }

	// State format: 'C' + opaque chunk, or 'P' + raw float parameter values.
	std::vector<uint8_t> getState() override
	{
		std::vector<uint8_t> state;
		if (effect->flags & effFlagsProgramChunks) {
			void *data = nullptr;
			intptr_t size = dispatch(effGetChunk, 0, 0, &data);
			if (data && size > 0) {
				state.push_back('C');
				state.insert(state.end(), (uint8_t *)data, (uint8_t *)data + size);
				return state;
			}
		}
		state.push_back('P');
		for (int i = 0; i < effect->numParams; i++) {
			float v = effect->getParameter(effect, i);
			state.insert(state.end(), (uint8_t *)&v, (uint8_t *)&v + sizeof(v));
		}
		return state;
	}

	bool setState(const std::vector<uint8_t> &state) override
	{
		if (state.empty())
			return false;
		if (state[0] == 'C') {
			std::vector<uint8_t> chunk(state.begin() + 1, state.end());
			dispatch(effSetChunk, 0, (intptr_t)chunk.size(), chunk.data());
			return true;
		}
		if (state[0] == 'P') {
			size_t count = std::min<size_t>((state.size() - 1) / sizeof(float), effect->numParams);
			for (size_t i = 0; i < count; i++) {
				float v;
				memcpy(&v, state.data() + 1 + i * sizeof(float), sizeof(v));
				effect->setParameter(effect, (int32_t)i, v);
			}
			return true;
		}
		return false;
	}

	bool hasEditor() override { return desc.hasEditor; }

	bool openEditor(void *parent, int &width, int &height) override
	{
		if (!desc.hasEditor || editorOpen)
			return false;
		ERect *rect = nullptr;
		dispatch(effEditGetRect, 0, 0, &rect); // some plugins only know their size after open
		dispatch(effEditOpen, 0, 0, parent);
		dispatch(effEditGetRect, 0, 0, &rect);
		width = rect ? rect->right - rect->left : 640;
		height = rect ? rect->bottom - rect->top : 480;
		editorOpen = true;
		return true;
	}

	void closeEditor() override
	{
		if (editorOpen)
			dispatch(effEditClose);
		editorOpen = false;
	}

	void editorIdle() override
	{
		if (editorOpen)
			dispatch(effEditIdle);
	}

	intptr_t onHostCall(int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt)
	{
		switch (opcode) {
		case audioMasterGetSampleRate:
			return (intptr_t)sampleRate;
		case audioMasterGetBlockSize:
			return blockSize;
		case audioMasterSizeWindow:
			if (onEditorResize)
				onEditorResize(index, (int)value);
			return 1;
		case audioMasterAutomate:
		case audioMasterEndEdit:
			if (onEdited)
				onEdited();
			return 0;
		default:
			(void)ptr;
			(void)opt;
			return 0;
		}
	}

	double sampleRate = 48000.0;
	int blockSize = 1024;

private:
	intptr_t dispatch(int32_t opcode, int32_t index = 0, intptr_t value = 0, void *ptr = nullptr, float opt = 0)
	{
		return effect->dispatcher(effect, opcode, index, value, ptr, opt);
	}

	std::string getString(int32_t opcode)
	{
		char buf[256] = {};
		dispatch(opcode, 0, 0, buf);
		buf[sizeof(buf) - 1] = 0;
		return buf;
	}

	HMODULE module = nullptr;
	AEffect *effect = nullptr;
	PluginDescription desc;
	ChannelMapper mapper;
	bool prepared = false;
	bool editorOpen = false;
};

intptr_t VST2_CALL hostCallback(AEffect *effect, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt)
{
	switch (opcode) {
	case audioMasterVersion:
		return 2400;
	case audioMasterGetVendorString:
		strcpy((char *)ptr, "OBS VST Ex");
		return 1;
	case audioMasterGetProductString:
		strcpy((char *)ptr, "OBS VST Ex Audio Filter");
		return 1;
	case audioMasterGetVendorVersion:
		return 1;
	case audioMasterCanDo:
		return ptr && (strcmp((const char *)ptr, "sizeWindow") == 0) ? 1 : 0;
	case audioMasterGetLanguage:
		return 1; // english
	default:
		break;
	}

	Vst2Instance *self = effect && effect->resvd1 ? (Vst2Instance *)effect->resvd1 : tlsLoading;
	return self ? self->onHostCall(opcode, index, value, ptr, opt) : 0;
}

} // namespace

std::unique_ptr<PluginInstance> loadVst2(const std::string &path, std::string &error)
{
	auto inst = std::make_unique<Vst2Instance>();
	if (!inst->load(path, error))
		return nullptr;
	return inst;
}

std::vector<PluginDescription> describeVst2(const std::string &path, std::string &error)
{
	auto inst = loadVst2(path, error);
	if (!inst)
		return {};
	return {inst->description()};
}

} // namespace vstex
