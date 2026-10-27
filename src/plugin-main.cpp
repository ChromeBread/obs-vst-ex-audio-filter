/*
OBS VST Ex Audio Filter
Copyright (C) 2026 BlackDream feat. ChromeBread

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "pitch-filter.h"
#include "stereo-filter.h"
#include "switch-sets.h"
#include "ui/ex-filters-dialog.h"
#include "ui/menu-hook.h"
#include "ui/fx-dock.h"
#include "ui/switch-dock.h"
#include "ui/tools-dialog.h"
#include "vst-filter.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

bool obs_module_load(void)
{
	apply_saved_language();
	register_vst_ex_filter();
	register_pitch_filter();
	register_stereo_filter();
	vst_ex_set_edited_callback(&ExFiltersDialog::notifyPluginEdited);
	install_mixer_menu_hook();
	install_tools_menu();
	switchsets::install();
	install_switch_dock();
	install_fx_dock();
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	remove_fx_dock();
	remove_switch_dock();
	switchsets::uninstall();
	remove_mixer_menu_hook();
	vst_ex_set_edited_callback(nullptr);
	obs_log(LOG_INFO, "plugin unloaded");
}
