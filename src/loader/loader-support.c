// plugin-support for the VST Enigmax Loader module (the generated one is named after VST Ex).
#include <plugin-support.h>

const char *PLUGIN_NAME = "vst-enigmax-loader";
const char *PLUGIN_VERSION = LOADER_VERSION;

void obs_log(int log_level, const char *format, ...)
{
	size_t length = 4 + strlen(PLUGIN_NAME) + strlen(format);
	char *template = malloc(length + 1);
	snprintf(template, length, "[%s] %s", PLUGIN_NAME, format);
	va_list(args);
	va_start(args, format);
	blogva(log_level, template, args);
	va_end(args);
	free(template);
}
