#include "input_settings.hpp"
#include <algorithm>

namespace gengine
{
    bool BindableKey(std::uint32_t key)
    {
        return (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || key == 0x20 || key == 0x10 || key == 0x11 ||
               (key >= 0x25 && key <= 0x28);
    }
    bool BindableButton(std::uint32_t button)
    {
        return button != 0 && (button & (button - 1)) == 0 && (button & 0x3f3f0) == button;
    }
    bool ValidInputSettings(const InputSettings& settings)
    {
        for (auto index = 0u; index < settings.keys.size(); ++index) {
            if (BindableKey(settings.keys[index]) == false ||
                std::find(settings.keys.begin(), settings.keys.begin() + index, settings.keys[index]) !=
                    settings.keys.begin() + index) {
                return false;
            }
        }
        for (auto index = 0u; index < settings.buttons.size(); ++index) {
            if (BindableButton(settings.buttons[index]) == false ||
                std::find(settings.buttons.begin(), settings.buttons.begin() + index, settings.buttons[index]) !=
                    settings.buttons.begin() + index) {
                return false;
            }
        }
        return true;
    }
    std::wstring InputKeyName(std::uint32_t key)
    {
        switch (key) {
        case 0x10:
            return L"Shift";
        case 0x11:
            return L"Ctrl";
        case 0x20:
            return L"Space";
        case 0x25:
            return L"Left";
        case 0x26:
            return L"Up";
        case 0x27:
            return L"Right";
        case 0x28:
            return L"Down";
        default:
            return BindableKey(key) ? std::wstring(1, static_cast<wchar_t>(key)) : L"?";
        }
    }
    std::wstring InputButtonName(std::uint32_t button)
    {
        switch (button) {
        case 0x10:
            return L"Start";
        case 0x20:
            return L"Back";
        case 0x40:
            return L"L Stick";
        case 0x80:
            return L"R Stick";
        case 0x100:
            return L"LB";
        case 0x200:
            return L"RB";
        case 0x1000:
            return L"A";
        case 0x2000:
            return L"B";
        case 0x4000:
            return L"X";
        case 0x8000:
            return L"Y";
        case kPadLeftTrigger:
            return L"LT";
        case kPadRightTrigger:
            return L"RT";
        default:
            return L"?";
        }
    }
} // namespace gengine
