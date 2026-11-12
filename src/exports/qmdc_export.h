// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#pragma once

// QMDC_API controls the visibility of the public C and C++ APIs.
//
// QMDC_STATIC       - defined while building/consuming the static library.
// QMDC_DLL_EXPORTS  - defined only while building the shared library itself.
#ifndef QMDC_STATIC
#ifdef _WIN32
#ifdef QMDC_DLL_EXPORTS
#define QMDC_API __declspec(dllexport)
#else
#define QMDC_API __declspec(dllimport)
#endif
#else
#define QMDC_API __attribute__((visibility("default")))
#endif
#else
#define QMDC_API
#endif
