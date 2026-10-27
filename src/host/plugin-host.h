#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vstex {

enum class PluginFormat { VST2, VST3 };

const char *formatName(PluginFormat format);

// One loadable effect. A VST3 module can contain several, a VST2 dll exactly one.
struct PluginDescription {
	PluginFormat format = PluginFormat::VST3;
	std::string path; // UTF-8
	std::string uid;  // VST3 class id (32 hex chars) or VST2 unique id (decimal)
	std::string name;
	std::string vendor;
	std::string category;
	std::string version;
	std::string description;
	std::string snapshot; // image shipped inside a VST3 bundle, if any
	int numInputs = 0;
	int numOutputs = 0;
	bool hasEditor = false;
};

// All calls except process() must come from one (UI) thread. process() runs on the audio thread;
// the owner is responsible for not calling it concurrently with prepare()/setState()/destruction.
class PluginInstance {
public:
	virtual ~PluginInstance() = default;

	virtual const PluginDescription &description() const = 0;

	virtual bool prepare(double sampleRate, int maxBlockSize, int numChannels) = 0;
	// In-place processing of planar float audio.
	virtual void process(float *const *channels, int numChannels, int frames) = 0;
	virtual int latencySamples() = 0;

	virtual std::vector<uint8_t> getState() = 0;
	virtual bool setState(const std::vector<uint8_t> &state) = 0;

	virtual bool hasEditor() = 0;
	// parent is an HWND. width/height receive the initial editor size.
	virtual bool openEditor(void *parent, int &width, int &height) = 0;
	virtual void closeEditor() = 0;
	virtual void editorIdle() {}

	// Set by the owner; the plugin asks the host to resize its editor window.
	std::function<void(int width, int height)> onEditorResize;
	// Set by the owner; the user changed something in the plugin (used for autosave).
	std::function<void()> onEdited;
};

// Lists the effects in a plugin file/bundle. Loads the plugin: call from the scanner process only.
std::vector<PluginDescription> describePlugins(const std::string &path, std::string &error);

// uid may be empty to load the first effect in the file.
std::unique_ptr<PluginInstance> loadPlugin(const std::string &path, const std::string &uid, std::string &error);

// Helpers implemented per format.
std::vector<PluginDescription> describeVst2(const std::string &path, std::string &error);
std::vector<PluginDescription> describeVst3(const std::string &path, std::string &error);
std::unique_ptr<PluginInstance> loadVst2(const std::string &path, std::string &error);
std::unique_ptr<PluginInstance> loadVst3(const std::string &path, const std::string &uid, std::string &error);

bool isVst3Path(const std::string &path);

// Copies planar OBS audio into a plugin's input/output layout and back.
// Plugins with fewer inputs than OBS channels get the first channels; extra plugin inputs are fed silence
// (or channel 0 for mono sources). A mono-output plugin is copied to every OBS channel.
class ChannelMapper {
public:
	void resize(int pluginInputs, int pluginOutputs, int maxFrames);
	void pull(float *const *obs, int obsChannels, int offset, int frames);
	void push(float *const *obs, int obsChannels, int offset, int frames) const;
	float **inputs() { return inPtrs.data(); }
	float **outputs() { return outPtrs.data(); }

private:
	std::vector<std::vector<float>> in, out;
	std::vector<float *> inPtrs, outPtrs;
};

} // namespace vstex
