// SPDX-License-Identifier: GPL-3.0-or-later
//
// udl-compat runtime plugin - the Nib plugin that re-adds legacy Notepad++ User-Defined
// Languages to wxNote's Scintillua engine.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).
//
// On load it asks the host (via nib.paths) for the user-data directory, reads every
// userDefineLang.xml under userDefineLangs/, translates each UDL into a Scintillua
// lexer (udl_parse + udl_scintillua), and registers it via nib.langdef. This is the one
// place that both knows the Notepad++ UDL format and produces Scintillua lexers - which
// is exactly why it is a separate, optional, GPL module rather than part of the core.

#include "nib.h"
#include "udl_parse.h"
#include "udl_scintillua.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static void activate(NibHost* host, NibQueryFn query)
{
    const auto* paths = static_cast<const NibPathsApi*>(query(host, NIB_IFACE_PATHS, 1));
    const auto* langs = static_cast<const NibLangDefApi*>(query(host, NIB_IFACE_LANGDEF, 1));
    if (!paths || !langs) return;

    char buf[4096];
    const int n = paths->user_data_dir(host, buf, static_cast<int>(sizeof(buf)));
    if (n <= 0) return;
    const std::string dir = std::string(buf) + "/userDefineLangs";

    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".xml") continue;
        const std::string xml = udlcompat::readFileToString(entry.path().string());
        if (xml.empty()) continue;

        udlcompat::UdlDef udl;
        if (!udlcompat::parseUserDefineLangXml(xml, udl)) continue;
        const std::string lua = udlcompat::translateUdlToScintillua(udl);
        langs->register_language(host, udl.name.c_str(), udl.ext.c_str(), lua.c_str());
    }
}

static const NibPluginApi g_api = {
    NIB_ABI_VERSION, sizeof(NibPluginApi), "org.wxnote.udl-compat", activate, nullptr
};

extern "C" NIB_API const NibPluginApi* nib_plugin_main(const NibBootstrap*) { return &g_api; }
