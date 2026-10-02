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
#include "yaml_io.h"

namespace {

// Trees made where they are used take rapidyaml's global callbacks: make those wxNote's quiet ones,
// the ones a Doc's tree has from the start (yaml_io.h).
[[maybe_unused]] const bool s_quietErrors = [] {
    ryml::set_callbacks(wxnyaml::detail::callbacks());
    return true;
}();

}   // namespace
