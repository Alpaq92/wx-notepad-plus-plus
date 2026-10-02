// SPDX-License-Identifier: Apache-2.0
//
// wxNote - the one translation unit that compiles rapidyaml.
// Copyright 2026 The wxNote Authors.
//
// rapidyaml's single-header release defines the library's functions only where
// RYML_SINGLE_HDR_DEFINE_NOW is set, so this file compiles them once and everything else includes
// ryml_all.hpp (through src/yaml_io.h) for the declarations alone. The header is fetched at
// configure time, pinned by version and hash - see the rapidyaml block in CMakeLists.txt.

#define RYML_SINGLE_HDR_DEFINE_NOW
#include "ryml_all.hpp"

namespace {

// rapidyaml's default error handlers print the error and its context to stderr before throwing.
// src/yaml_io.h catches every one of these and reports it to its caller (file, line, column), so the
// print is only noise - in a test log, or in the terminal a Linux user started wxNote from. These throw
// the same exception types without it. Installed once, before main(): this object file is always
// linked, because it holds the library itself.
[[noreturn]] void quietBasic(ryml::csubstr msg, ryml::ErrorDataBasic const& data, void*)
{
    throw ryml::ExceptionBasic(msg, data);
}
[[noreturn]] void quietParse(ryml::csubstr msg, ryml::ErrorDataParse const& data, void*)
{
    throw ryml::ExceptionParse(msg, data);
}
[[noreturn]] void quietVisit(ryml::csubstr msg, ryml::ErrorDataVisit const& data, void*)
{
    throw ryml::ExceptionVisit(msg, data);
}

[[maybe_unused]] const bool s_quietErrors = [] {
    ryml::Callbacks cb = ryml::get_callbacks();
    cb.set_error_basic(&quietBasic).set_error_parse(&quietParse).set_error_visit(&quietVisit);
    ryml::set_callbacks(cb);
    return true;
}();

}   // namespace
