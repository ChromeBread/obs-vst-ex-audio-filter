#include "host/plugin-host.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace vstex {

const char *formatName(PluginFormat format)
{
	return format == PluginFormat::VST2 ? "VST2" : "VST3";
}

bool isVst3Path(const std::string &path)
{
	if (path.size() < 5)
		return false;
	std::string ext = path.substr(path.size() - 5);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return ext == ".vst3";
}

std::vector<PluginDescription> describePlugins(const std::string &path, std::string &error)
{
	return isVst3Path(path) ? describeVst3(path, error) : describeVst2(path, error);
}

std::unique_ptr<PluginInstance> loadPlugin(const std::string &path, const std::string &uid, std::string &error)
{
	return isVst3Path(path) ? loadVst3(path, uid, error) : loadVst2(path, error);
}

void ChannelMapper::resize(int pluginInputs, int pluginOutputs, int maxFrames)
{
	in.assign(pluginInputs, std::vector<float>(maxFrames, 0.0f));
	out.assign(pluginOutputs, std::vector<float>(maxFrames, 0.0f));
	inPtrs.clear();
	outPtrs.clear();
	for (auto &b : in)
		inPtrs.push_back(b.data());
	for (auto &b : out)
		outPtrs.push_back(b.data());
}

void ChannelMapper::pull(float *const *obs, int obsChannels, int offset, int frames)
{
	for (size_t i = 0; i < in.size(); i++) {
		int src = (int)i < obsChannels ? (int)i : (obsChannels == 1 ? 0 : -1);
		if (src >= 0)
			memcpy(in[i].data(), obs[src] + offset, frames * sizeof(float));
		else
			memset(in[i].data(), 0, frames * sizeof(float));
	}
	for (auto &b : out)
		memset(b.data(), 0, frames * sizeof(float));
}

void ChannelMapper::push(float *const *obs, int obsChannels, int offset, int frames) const
{
	if (out.empty())
		return;
	for (int c = 0; c < obsChannels; c++) {
		if (c < (int)out.size())
			memcpy(obs[c] + offset, out[c].data(), frames * sizeof(float));
		else if (out.size() == 1)
			memcpy(obs[c] + offset, out[0].data(), frames * sizeof(float));
		// else: plugin has no output for this channel, leave it unprocessed
	}
}

} // namespace vstex
