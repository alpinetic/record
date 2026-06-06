#pragma once

#include <windows.h>
#include <flutter/encodable_value.h>
#include <string>

namespace record_windows {
namespace AudioDevice {

HRESULT ListInputDevices(flutter::EncodableList& devices);
HRESULT IsEncoderSupported(const std::string& encoderName, bool* supported);

} // namespace AudioDevice
} // namespace record_windows
