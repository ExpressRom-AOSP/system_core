/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

class FastbootDevice;

std::string GetVersion(FastbootDevice* device, const std::vector<std::string>& args);
std::string GetBootloaderVersion(FastbootDevice* device, const std::vector<std::string>& args);
std::string GetBasebandVersion(FastbootDevice* device, const std::vector<std::string>& args);
std::string GetProduct(FastbootDevice* device, const std::vector<std::string>& args);
std::string GetSerial(FastbootDevice* device, const std::vector<std::string>& args);
std::string GetSecure(FastbootDevice* device, const std::vector<std::string>& args);
std::string GetCurrentSlot(FastbootDevice* device, const std::vector<std::string>& args);
std::string GetSlotCount(FastbootDevice* device, const std::vector<std::string>& args);
std::string GetSlotSuccessful(FastbootDevice* device, const std::vector<std::string>& args);
std::string GetSlotUnbootable(FastbootDevice* device, const std::vector<std::string>& args);
std::string GetMaxDownloadSize(FastbootDevice* device, const std::vector<std::string>& args);
std::string GetUnlocked(FastbootDevice* device, const std::vector<std::string>& args);
std::string GetHasSlot(FastbootDevice* device, const std::vector<std::string>& args);
