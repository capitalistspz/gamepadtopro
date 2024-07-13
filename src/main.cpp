#include <malloc.h>
#include <wups.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemIntegerRange.h>
#include <wups/config_api.h>
#include <padscore/wpad.h>
#include <padscore/kpad.h>
#include <vpad/input.h>
#include <coreinit/title.h>
#include <mutex>
#include "padscore_extra.h"

WUPS_PLUGIN_NAME("gamepadtopro");
WUPS_PLUGIN_DESCRIPTION("Maps Wii U GamePad as a Pro Controller");
WUPS_PLUGIN_VERSION("v1.1.1");
WUPS_PLUGIN_AUTHOR("capitalistspz");
WUPS_PLUGIN_LICENSE("MIT");

#define ENABLE_0_CONFIG_ID "enable0"
#define CHANNEL_0_CONFIG_ID "channel0"

#define ENABLE_1_CONFIG_ID "enable1"
#define CHANNEL_1_CONFIG_ID "channel1"

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("gamepadtopro");

struct Setting {
    bool enable;
    int channel;
} settings[2];

//! Info
uint64_t applicationTitleId = 0;
bool configMenuOpen = false;

VPADStatus vpadBuffers[2][32];
uint32_t vpadBufferCount = 0;
std::mutex vpadBufferMutex;

bool IsGame(uint64_t id) {
    return (id & 0xFFFFFFFF'00000000ULL) == 0x00050000'00000000ULL;
}

bool UseReal() {
    return !IsGame(applicationTitleId) || configMenuOpen || !WPADIsEnabledURC();
}

bool GetChanIfAllowed(KPADChan chan, VPADChan &vpadChan) {
    if (UseReal())
        return false;
    if (settings[0].enable && settings[0].channel == chan) {
        vpadChan = VPAD_CHAN_0;
        return true;
    } else if (settings[1].enable && settings[1].channel == chan) {
        vpadChan = VPAD_CHAN_1;
        return true;
    }
    return false;
}

void channelItemChanged(ConfigItemIntegerRange *item, int newValue) {
    if (std::string_view(CHANNEL_0_CONFIG_ID) == item->identifier) {
        settings[0].channel = newValue;
        WUPSStorageAPI::Store(item->identifier, newValue);
    }
    if (std::string_view(CHANNEL_1_CONFIG_ID) == item->identifier) {
        settings[1].channel = newValue;
        WUPSStorageAPI::Store(item->identifier, newValue);
    }
}

void enablePluginItemChanged(ConfigItemBoolean *item, bool newValue) {
    if (std::string_view(ENABLE_0_CONFIG_ID) == item->identifier) {
        settings[0].enable = newValue;
        WUPSStorageAPI::Store(item->identifier, newValue);
    }
    if (std::string_view(ENABLE_1_CONFIG_ID) == item->identifier) {
        settings[1].enable = newValue;
        WUPSStorageAPI::Store(item->identifier, newValue);
    }
}

WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle) {
    configMenuOpen = true;
    // To use the C++ API, we create new WUPSConfigCategory from the root handle!
    WUPSConfigCategory root = WUPSConfigCategory(rootHandle);

    try {
        root.add(WUPSConfigItemBoolean::Create(ENABLE_0_CONFIG_ID, "Gamepad 0 Enable",
                                               false, settings[0].enable,
                                               &enablePluginItemChanged));

        root.add(WUPSConfigItemIntegerRange::Create(CHANNEL_0_CONFIG_ID, "Gamepad 0 Channel",
                                                    0, settings[0].channel,
                                                    0, 6,
                                                    &channelItemChanged));

        root.add(WUPSConfigItemBoolean::Create(ENABLE_1_CONFIG_ID, "Gamepad 1 Enable",
                                               false, settings[1].enable,
                                               &enablePluginItemChanged));

        root.add(WUPSConfigItemIntegerRange::Create(CHANNEL_1_CONFIG_ID, "Gamepad 1 Channel",
                                                    0, settings[1].channel,
                                                    0, 6,
                                                    &channelItemChanged));

    } catch (std::exception &e) {
        return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
    }

    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}

void ConfigMenuClosedCallback() {
    WUPSStorageAPI::SaveStorage();
    configMenuOpen = false;
}

INITIALIZE_PLUGIN() {
    WUPSConfigAPIOptionsV1 configOptions = {.name = "gamepadtopro"};
    WUPSConfigAPI_Init(configOptions, ConfigMenuOpenedCallback, ConfigMenuClosedCallback);

    WUPSStorageAPI::GetOrStoreDefault(ENABLE_0_CONFIG_ID, settings[0].enable, false);
    WUPSStorageAPI::GetOrStoreDefault(CHANNEL_0_CONFIG_ID, settings[0].channel, 0);
    WUPSStorageAPI::GetOrStoreDefault(ENABLE_1_CONFIG_ID, settings[1].enable, false);
    WUPSStorageAPI::GetOrStoreDefault(CHANNEL_1_CONFIG_ID, settings[1].channel, 0);
    WUPSStorageAPI::SaveStorage();
}

ON_APPLICATION_START() {
    applicationTitleId = OSGetTitleID();
}

ON_APPLICATION_REQUESTS_EXIT() {
    applicationTitleId = 0;
}

DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffers, uint32_t count, VPADReadError *outError) {
    if (!settings[chan].enable || UseReal())
        return real_VPADRead(chan, buffers, count, outError);
    bzero(buffers, sizeof(VPADStatus) * count);
    *outError = VPAD_READ_NO_SAMPLES;

    return 0;
}

DECL_FUNCTION(int32_t, KPADReadEx, KPADChan channel, KPADStatus *data, uint32_t size, KPADError *outError) {
    VPADChan vpadChan;
    if (!GetChanIfAllowed(channel, vpadChan))
        return real_KPADReadEx(channel, data, size, outError);
    if (data == nullptr || size == 0) {
        *outError = KPAD_ERROR_NO_SAMPLES;
        return 0;
    }

    auto lock = std::scoped_lock(vpadBufferMutex);
    vpadBufferCount = std::min(size, 32u);
    VPADReadError error;
    const auto count = real_VPADRead(vpadChan, vpadBuffers[vpadChan], vpadBufferCount, &error);
    if (error != VPADReadError::VPAD_READ_SUCCESS || count == 0) {
        switch (error) {
            case VPAD_READ_NO_SAMPLES:
                *outError = KPAD_ERROR_NO_SAMPLES;
                break;
            case VPAD_READ_INVALID_CONTROLLER:
                *outError = KPAD_ERROR_INVALID_CONTROLLER;
                break;
            case VPAD_READ_BUSY:
                *outError = KPAD_ERROR_BUSY;
                break;
            default:
                *outError = KPAD_ERROR_OK;
                break;
        }
        return 0;
    }

#define COPY_BTNK(dest_btn, src_btn)                            \
kpad.pro.hold |= (vpad.hold & src_btn) ? dest_btn : 0;          \
kpad.pro.trigger |= (vpad.trigger & src_btn) ? dest_btn : 0;    \
kpad.pro.release |= (vpad.release & src_btn) ? dest_btn : 0

    for (auto i = 0; i < count; ++i) {
        auto const &vpad = vpadBuffers[vpadChan][i];
        auto &kpad = data[i];

        bzero(&kpad, sizeof(KPADStatus));

        COPY_BTNK(WPAD_PRO_BUTTON_A, VPAD_BUTTON_A);
        COPY_BTNK(WPAD_PRO_BUTTON_B, VPAD_BUTTON_B);
        COPY_BTNK(WPAD_PRO_BUTTON_X, VPAD_BUTTON_X);
        COPY_BTNK(WPAD_PRO_BUTTON_Y, VPAD_BUTTON_Y);

        COPY_BTNK(WPAD_PRO_BUTTON_LEFT, VPAD_BUTTON_LEFT);
        COPY_BTNK(WPAD_PRO_BUTTON_RIGHT, VPAD_BUTTON_RIGHT);
        COPY_BTNK(WPAD_PRO_BUTTON_DOWN, VPAD_BUTTON_DOWN);
        COPY_BTNK(WPAD_PRO_BUTTON_UP, VPAD_BUTTON_UP);

        COPY_BTNK(WPAD_PRO_BUTTON_STICK_L, VPAD_BUTTON_STICK_L);
        COPY_BTNK(WPAD_PRO_BUTTON_STICK_R, VPAD_BUTTON_STICK_R);

        COPY_BTNK(WPAD_PRO_BUTTON_MINUS, VPAD_BUTTON_MINUS);
        COPY_BTNK(WPAD_PRO_BUTTON_PLUS, VPAD_BUTTON_PLUS);
        COPY_BTNK(WPAD_PRO_BUTTON_HOME, VPAD_BUTTON_HOME);

        COPY_BTNK(WPAD_PRO_TRIGGER_L, VPAD_BUTTON_L);
        COPY_BTNK(WPAD_PRO_TRIGGER_R, VPAD_BUTTON_R);
        COPY_BTNK(WPAD_PRO_TRIGGER_ZL, VPAD_BUTTON_ZL);
        COPY_BTNK(WPAD_PRO_TRIGGER_ZR, VPAD_BUTTON_ZR);

        COPY_BTNK(WPAD_PRO_STICK_L_EMULATION_LEFT, VPAD_STICK_L_EMULATION_LEFT);
        COPY_BTNK(WPAD_PRO_STICK_L_EMULATION_RIGHT, VPAD_STICK_L_EMULATION_RIGHT);
        COPY_BTNK(WPAD_PRO_STICK_L_EMULATION_DOWN, VPAD_STICK_L_EMULATION_DOWN);
        COPY_BTNK(WPAD_PRO_STICK_L_EMULATION_UP, VPAD_STICK_L_EMULATION_UP);

        COPY_BTNK(WPAD_PRO_STICK_R_EMULATION_LEFT, VPAD_STICK_R_EMULATION_LEFT);
        COPY_BTNK(WPAD_PRO_STICK_R_EMULATION_RIGHT, VPAD_STICK_R_EMULATION_RIGHT);
        COPY_BTNK(WPAD_PRO_STICK_R_EMULATION_DOWN, VPAD_STICK_R_EMULATION_DOWN);
        COPY_BTNK(WPAD_PRO_STICK_R_EMULATION_UP, VPAD_STICK_R_EMULATION_UP);
#undef COPY_BTNK

        kpad.pro.leftStick.x = vpad.leftStick.x;
        kpad.pro.leftStick.y = vpad.leftStick.y;
        kpad.pro.rightStick.x = vpad.rightStick.x;
        kpad.pro.rightStick.y = vpad.rightStick.y;
        kpad.pro.charging = false;
        kpad.format = KPADDataFormat::WPAD_FMT_PRO_CONTROLLER;
        kpad.extensionType = KPADExtensionType::WPAD_EXT_PRO_CONTROLLER;
        kpad.error = KPADError::KPAD_ERROR_OK;
        kpad.pro.wired = false;
    }
    return count;
}

DECL_FUNCTION(int32_t, KPADRead, KPADChan channel, KPADStatus *data, uint32_t size) {
    KPADError errorOut;
    return my_KPADReadEx(channel, data, size, &errorOut);
}

DECL_FUNCTION(int32_t, WPADProbe, WPADChan chan, WPADExtensionType *outExtensionType) {
    VPADChan vpadChan;
    if (!GetChanIfAllowed(chan, vpadChan))
        return real_WPADProbe(chan, outExtensionType);
    *outExtensionType = WPADExtensionType::WPAD_EXT_PRO_CONTROLLER;
    return 0;
}

DECL_FUNCTION(void, WPADControlMotor, WPADChan chan, BOOL enabled) {
    constexpr static uint8_t pattern[15] = {0xFF, 0xFF};
    VPADChan vpadChan;
    if (!GetChanIfAllowed(chan, vpadChan))
        real_WPADControlMotor(chan, enabled);
    else if (enabled && WPADIsMotorEnabled())
        VPADControlMotor((VPADChan) vpadChan, (uint8_t *) &pattern, 2);
    else
        VPADStopMotor((VPADChan) vpadChan);

}

DECL_FUNCTION(WPADDataFormat, WPADGetDataFormat, WPADChan chan) {
    VPADChan vpadChan;
    if (!GetChanIfAllowed(chan, vpadChan))
        return real_WPADGetDataFormat(chan);
    return WPADDataFormat::WPAD_FMT_PRO_CONTROLLER;
}

DECL_FUNCTION(void, WPADRead, WPADChan chan, void *buffer) {
    VPADChan vpadChan;
    if (!GetChanIfAllowed(chan, vpadChan)) {
        real_WPADRead(chan, buffer);
        return;
    }

    VPADStatus vpadStatus;
    VPADReadError error;
    const auto count = real_VPADRead(vpadChan, &vpadStatus, 1, &error);

    auto *wpadStatus = (WPADStatusProController *) buffer;
    bzero(wpadStatus, sizeof(WPADStatusProController));

    if (count <= 0 || error != VPAD_READ_SUCCESS) {
        //! no controller
        wpadStatus->err = -1;
        wpadStatus->extensionType = 0xfd;
        return;
    }
    wpadStatus->err = 0;
    wpadStatus->extensionType = WPAD_EXT_PRO_CONTROLLER;
    wpadStatus->dataFormat = WPAD_FMT_PRO_CONTROLLER;

#define COPY_BTNW(dest_btn, src_btn) wpadStatus->buttons |= (vpadStatus.hold & src_btn || vpadStatus.trigger & src_btn) ? dest_btn : 0
    COPY_BTNW(WPAD_PRO_BUTTON_A, VPAD_BUTTON_A);
    COPY_BTNW(WPAD_PRO_BUTTON_B, VPAD_BUTTON_B);
    COPY_BTNW(WPAD_PRO_BUTTON_X, VPAD_BUTTON_X);
    COPY_BTNW(WPAD_PRO_BUTTON_Y, VPAD_BUTTON_Y);

    COPY_BTNW(WPAD_PRO_BUTTON_LEFT, VPAD_BUTTON_LEFT);
    COPY_BTNW(WPAD_PRO_BUTTON_RIGHT, VPAD_BUTTON_RIGHT);
    COPY_BTNW(WPAD_PRO_BUTTON_DOWN, VPAD_BUTTON_DOWN);
    COPY_BTNW(WPAD_PRO_BUTTON_UP, VPAD_BUTTON_UP);

    COPY_BTNW(WPAD_PRO_BUTTON_STICK_L, VPAD_BUTTON_STICK_L);
    COPY_BTNW(WPAD_PRO_BUTTON_STICK_R, VPAD_BUTTON_STICK_R);

    COPY_BTNW(WPAD_PRO_BUTTON_MINUS, VPAD_BUTTON_MINUS);
    COPY_BTNW(WPAD_PRO_BUTTON_PLUS, VPAD_BUTTON_PLUS);
    COPY_BTNW(WPAD_PRO_BUTTON_HOME, VPAD_BUTTON_HOME);

    COPY_BTNW(WPAD_PRO_TRIGGER_L, VPAD_BUTTON_L);
    COPY_BTNW(WPAD_PRO_TRIGGER_R, VPAD_BUTTON_R);
    COPY_BTNW(WPAD_PRO_TRIGGER_ZL, VPAD_BUTTON_ZL);
    COPY_BTNW(WPAD_PRO_TRIGGER_ZR, VPAD_BUTTON_ZR);

    COPY_BTNW(WPAD_PRO_STICK_L_EMULATION_LEFT, VPAD_STICK_L_EMULATION_LEFT);
    COPY_BTNW(WPAD_PRO_STICK_L_EMULATION_RIGHT, VPAD_STICK_L_EMULATION_RIGHT);
    COPY_BTNW(WPAD_PRO_STICK_L_EMULATION_DOWN, VPAD_STICK_L_EMULATION_DOWN);
    COPY_BTNW(WPAD_PRO_STICK_L_EMULATION_UP, VPAD_STICK_L_EMULATION_UP);

    COPY_BTNW(WPAD_PRO_STICK_R_EMULATION_LEFT, VPAD_STICK_R_EMULATION_LEFT);
    COPY_BTNW(WPAD_PRO_STICK_R_EMULATION_RIGHT, VPAD_STICK_R_EMULATION_RIGHT);
    COPY_BTNW(WPAD_PRO_STICK_R_EMULATION_DOWN, VPAD_STICK_R_EMULATION_DOWN);
    COPY_BTNW(WPAD_PRO_STICK_R_EMULATION_UP, VPAD_STICK_R_EMULATION_UP);
#undef COPY_BTNW

    wpadStatus->rightStick.x = vpadStatus.rightStick.x * (1 << 15);
    wpadStatus->rightStick.y = vpadStatus.rightStick.y * (1 << 15);

    wpadStatus->leftStick.x = vpadStatus.leftStick.x * (1 << 15);
    wpadStatus->leftStick.y = vpadStatus.leftStick.y * (1 << 15);
}

DECL_FUNCTION(uint8_t, WPADGetBatteryLevel, WPADChan chan) {
    VPADChan vpadChan;
    if (!GetChanIfAllowed(chan, vpadChan))
        return real_WPADGetBatteryLevel(chan);
    // Full VPAD battery is 0xC0 and full WPAD battery is 0x4
    return vpadBuffers[vpadChan][std::max((int)vpadBufferCount - 1, 0)].battery / 0x30;
}

WUPS_MUST_REPLACE(KPADReadEx, WUPS_LOADER_LIBRARY_PADSCORE, KPADReadEx);
WUPS_MUST_REPLACE(KPADRead, WUPS_LOADER_LIBRARY_PADSCORE, KPADRead);

WUPS_MUST_REPLACE(WPADProbe, WUPS_LOADER_LIBRARY_PADSCORE, WPADProbe);
WUPS_MUST_REPLACE(WPADRead, WUPS_LOADER_LIBRARY_PADSCORE, WPADRead);

WUPS_MUST_REPLACE(WPADControlMotor, WUPS_LOADER_LIBRARY_PADSCORE, WPADControlMotor);
WUPS_MUST_REPLACE(WPADGetDataFormat, WUPS_LOADER_LIBRARY_PADSCORE, WPADGetDataFormat);
WUPS_MUST_REPLACE(WPADGetBatteryLevel, WUPS_LOADER_LIBRARY_PADSCORE, WPADGetBatteryLevel);

WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);