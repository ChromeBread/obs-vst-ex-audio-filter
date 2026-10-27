#include "host/plugin-host.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace vstex {

namespace {

void ensureHostContext()
{
	static IPtr<HostApplication> app = [] {
		auto a = owned(new HostApplication());
		PluginContextFactory::instance().setPluginContext(a);
		return a;
	}();
}

// Keep modules loaded for the lifetime of the process; unloading plugin dlls is a common source of crashes.
VST3::Hosting::Module::Ptr openModule(const std::string &path, std::string &error)
{
	static std::map<std::string, VST3::Hosting::Module::Ptr> cache;
	auto it = cache.find(path);
	if (it != cache.end())
		return it->second;
	auto module = VST3::Hosting::Module::create(path, error);
	if (module)
		cache[path] = module;
	return module;
}

PluginDescription describe(const std::string &path, const VST3::Hosting::ClassInfo &ci)
{
	PluginDescription d;
	d.format = PluginFormat::VST3;
	d.path = path;
	d.uid = ci.ID().toString();
	d.name = ci.name();
	d.vendor = ci.vendor();
	d.version = ci.version();
	// "Fx|Dynamics" -> "Dynamics"; plain "Fx" stays "Fx"
	std::string sub = ci.subCategoriesString();
	if (sub.rfind("Fx|", 0) == 0)
		sub = sub.substr(3);
	d.category = sub;
	d.hasEditor = true; // only known after creating a view
	return d;
}

class ComponentHandler final : public U::Implements<U::Directly<IComponentHandler>> {
public:
	tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
	tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			pending.push_back({id, value});
		}
		return kResultOk;
	}
	tresult PLUGIN_API endEdit(ParamID) override
	{
		if (onEdited)
			onEdited();
		return kResultOk;
	}
	tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }

	// Audio thread: moves queued edits into the processor's input parameter queue without blocking.
	void drainInto(ParameterChanges &changes)
	{
		std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
		if (!lock.owns_lock())
			return;
		for (auto &p : pending) {
			int32 index = 0;
			if (auto *queue = changes.addParameterData(p.first, index))
				queue->addPoint(0, p.second, index);
		}
		pending.clear();
	}

	std::function<void()> onEdited;

private:
	std::mutex mutex;
	std::vector<std::pair<ParamID, ParamValue>> pending;
};

class PlugFrame final : public U::Implements<U::Directly<IPlugFrame>> {
public:
	tresult PLUGIN_API resizeView(IPlugView *view, ViewRect *newSize) override
	{
		if (!view || !newSize)
			return kInvalidArgument;
		if (onResize)
			onResize(newSize->getWidth(), newSize->getHeight());
		view->onSize(newSize);
		return kResultTrue;
	}
	std::function<void(int, int)> onResize;
};

class Vst3Instance final : public PluginInstance {
public:
	~Vst3Instance() override
	{
		closeEditor();
		if (processor && prepared)
			processor->setProcessing(false);
		if (component && prepared)
			component->setActive(false);
		data.unprepare();
		processor = nullptr;
		controller = nullptr;
		component = nullptr;
		provider = nullptr; // terminates and disconnects component/controller
	}

	bool load(const std::string &path, const std::string &uid, std::string &error)
	{
		ensureHostContext();
		module = openModule(path, error);
		if (!module)
			return false;

		const auto &factory = module->getFactory();
		for (auto &ci : factory.classInfos()) {
			if (ci.category() != kVstAudioEffectClass)
				continue;
			if (!uid.empty() && ci.ID().toString() != uid)
				continue;
			provider = owned(new PlugProvider(factory, ci, true));
			if (!provider->initialize()) {
				error = "failed to initialize " + ci.name();
				return false;
			}
			desc = describe(path, ci);
			break;
		}
		if (!provider) {
			error = uid.empty() ? "no audio effect class in module" : "class " + uid + " not found in module";
			return false;
		}

		component = provider->getComponentPtr();
		controller = provider->getControllerPtr();
		processor = U::cast<IAudioProcessor>(component);
		if (!component || !processor) {
			error = "plugin has no audio processor";
			return false;
		}

		handler->onEdited = [this] {
			if (onEdited)
				onEdited();
		};
		frame->onResize = [this](int w, int h) {
			if (onEditorResize)
				onEditorResize(w, h);
		};
		if (controller)
			controller->setComponentHandler(handler);

		desc.numInputs = busChannels(kInput);
		desc.numOutputs = busChannels(kOutput);
		return true;
	}

	const PluginDescription &description() const override { return desc; }

	bool prepare(double sampleRate, int maxBlock, int numChannels) override
	{
		blockSize = maxBlock;
		if (prepared) {
			processor->setProcessing(false);
			component->setActive(false);
			data.unprepare();
		}

		// Ask for the OBS layout on the main buses; keep whatever the plugin wants elsewhere.
		SpeakerArrangement want = numChannels == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
		std::vector<SpeakerArrangement> ins(component->getBusCount(kAudio, kInput));
		std::vector<SpeakerArrangement> outs(component->getBusCount(kAudio, kOutput));
		for (size_t i = 0; i < ins.size(); i++)
			if (i == 0 || processor->getBusArrangement(kInput, (int32)i, ins[i]) != kResultOk)
				ins[i] = want;
		for (size_t i = 0; i < outs.size(); i++)
			if (i == 0 || processor->getBusArrangement(kOutput, (int32)i, outs[i]) != kResultOk)
				outs[i] = want;
		processor->setBusArrangements(ins.data(), (int32)ins.size(), outs.data(), (int32)outs.size());
		if (!ins.empty())
			component->activateBus(kAudio, kInput, 0, true);
		if (!outs.empty())
			component->activateBus(kAudio, kOutput, 0, true);

		ProcessSetup setup{kRealtime, kSample32, maxBlock, sampleRate};
		if (processor->setupProcessing(setup) != kResultOk)
			return false;
		if (component->setActive(true) != kResultOk)
			return false;

		data.prepare(*component, maxBlock, kSample32);
		data.inputParameterChanges = &inChanges;
		data.outputParameterChanges = &outChanges;
		memset(&context, 0, sizeof(context));
		context.sampleRate = sampleRate;
		context.tempo = 120.0;
		context.state = ProcessContext::kTempoValid;
		data.processContext = &context;

		processor->setProcessing(true);
		desc.numInputs = busChannels(kInput);
		desc.numOutputs = busChannels(kOutput);
		prepared = true;
		return true;
	}

	void process(float *const *channels, int numChannels, int frames) override
	{
		if (!prepared || data.numInputs < 1 || data.numOutputs < 1)
			return;
		AudioBusBuffers &in = data.inputs[0];
		AudioBusBuffers &out = data.outputs[0];

		for (int offset = 0; offset < frames; offset += blockSize) {
			int n = std::min(blockSize, frames - offset);
			for (int32 b = 0; b < data.numInputs; b++)
				for (int32 c = 0; c < data.inputs[b].numChannels; c++)
					memset(data.inputs[b].channelBuffers32[c], 0, n * sizeof(float));
			for (int32 c = 0; c < in.numChannels; c++) {
				int src = c < numChannels ? c : (numChannels == 1 ? 0 : -1);
				if (src >= 0)
					memcpy(in.channelBuffers32[c], channels[src] + offset, n * sizeof(float));
			}

			inChanges.clearQueue();
			outChanges.clearQueue();
			handler->drainInto(inChanges);
			data.numSamples = n;
			if (processor->process(data) != kResultOk)
				return;
			context.projectTimeSamples += n;

			for (int c = 0; c < numChannels; c++) {
				int src = c < out.numChannels ? c : (out.numChannels == 1 ? 0 : -1);
				if (src >= 0)
					memcpy(channels[c] + offset, out.channelBuffers32[src], n * sizeof(float));
			}
		}
	}

	int latencySamples() override { return processor ? (int)processor->getLatencySamples() : 0; }

	// State format: uint32 component state size, component state, controller state.
	std::vector<uint8_t> getState() override
	{
		auto compStream = owned(new MemoryStream());
		auto ctrlStream = owned(new MemoryStream());
		component->getState(compStream);
		if (controller)
			controller->getState(ctrlStream);

		uint32_t compSize = (uint32_t)compStream->getSize();
		std::vector<uint8_t> state(sizeof(compSize));
		memcpy(state.data(), &compSize, sizeof(compSize));
		state.insert(state.end(), compStream->getData(), compStream->getData() + compSize);
		state.insert(state.end(), ctrlStream->getData(), ctrlStream->getData() + ctrlStream->getSize());
		return state;
	}

	bool setState(const std::vector<uint8_t> &state) override
	{
		uint32_t compSize = 0;
		if (state.size() < sizeof(compSize))
			return false;
		memcpy(&compSize, state.data(), sizeof(compSize));
		if (compSize > state.size() - sizeof(compSize))
			return false;

		auto *compData = (void *)(state.data() + sizeof(compSize));
		auto compStream = owned(new MemoryStream(compData, compSize));
		// Result ignored: some plugins return kResultFalse or kNotImplemented even when they restored fine.
		component->setState(compStream);
		if (controller) {
			compStream->seek(0, IBStream::kIBSeekSet, nullptr);
			controller->setComponentState(compStream);
			size_t ctrlSize = state.size() - sizeof(compSize) - compSize;
			if (ctrlSize > 0) {
				auto ctrlStream = owned(new MemoryStream((char *)compData + compSize, (TSize)ctrlSize));
				controller->setState(ctrlStream);
			}
		}
		return true;
	}

	bool hasEditor() override { return controller != nullptr; }

	bool openEditor(void *parent, int &width, int &height) override
	{
		if (!controller || view)
			return false;
		view = owned(controller->createView(ViewType::kEditor));
		if (!view || view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) {
			view = nullptr;
			return false;
		}
		view->setFrame(frame);
		if (view->attached(parent, kPlatformTypeHWND) != kResultOk) {
			view->setFrame(nullptr);
			view = nullptr;
			return false;
		}
		ViewRect rect;
		view->getSize(&rect);
		width = rect.getWidth();
		height = rect.getHeight();
		return true;
	}

	void closeEditor() override
	{
		if (!view)
			return;
		view->removed();
		view->setFrame(nullptr);
		view = nullptr;
	}

private:
	int busChannels(BusDirection dir)
	{
		if (component->getBusCount(kAudio, dir) < 1)
			return 0;
		BusInfo info{};
		component->getBusInfo(kAudio, dir, 0, info);
		return info.channelCount;
	}

	VST3::Hosting::Module::Ptr module;
	IPtr<PlugProvider> provider;
	IPtr<IComponent> component;
	IPtr<IAudioProcessor> processor;
	IPtr<IEditController> controller;
	IPtr<IPlugView> view;
	IPtr<ComponentHandler> handler = owned(new ComponentHandler());
	IPtr<PlugFrame> frame = owned(new PlugFrame());

	HostProcessData data;
	ParameterChanges inChanges{64};
	ParameterChanges outChanges{64};
	ProcessContext context{};
	PluginDescription desc;
	int blockSize = 1024;
	bool prepared = false;
};

} // namespace

std::unique_ptr<PluginInstance> loadVst3(const std::string &path, const std::string &uid, std::string &error)
{
	auto inst = std::make_unique<Vst3Instance>();
	if (!inst->load(path, uid, error))
		return nullptr;
	return inst;
}

std::vector<PluginDescription> describeVst3(const std::string &path, std::string &error)
{
	auto module = openModule(path, error);
	if (!module)
		return {};
	std::vector<PluginDescription> list;
	auto snapshots = VST3::Hosting::Module::getSnapshots(path);
	for (auto &ci : module->getFactory().classInfos()) {
		if (ci.category() != kVstAudioEffectClass)
			continue;
		PluginDescription d = describe(path, ci);
		// Not instantiated here: shells like WaveShell hold hundreds of classes. Instruments are flagged by category.
		bool instrument = ci.subCategoriesString().find("Instrument") != std::string::npos;
		d.numInputs = instrument ? 0 : 2;
		d.numOutputs = 2;
		for (auto &snap : snapshots) {
			if (snap.uid.toString() != d.uid)
				continue;
			for (auto &img : snap.images) // smallest scale is enough for a thumbnail
				if (d.snapshot.empty() || img.scaleFactor == 1.0)
					d.snapshot = img.path;
		}
		list.push_back(d);
	}
	if (list.empty())
		error = "no audio effect class in module";
	return list;
}

} // namespace vstex
