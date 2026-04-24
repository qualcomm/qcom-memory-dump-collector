// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#include "ProgressCallback.h"

namespace QC {
namespace CLI {

ProgressCallbackManager& ProgressCallbackManager::getInstance()
{
   static ProgressCallbackManager instance;
   return instance;
}

void ProgressCallbackManager::reportProgress(int percentage, const std::string& operation, const std::string& details)
{
   (void)percentage;
   (void)operation;
   (void)details;
}

} // namespace CLI
} // namespace QC
