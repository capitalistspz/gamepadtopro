#include <malloc.h>
#include <wups.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemIntegerRange.h>
#include <wups/config_api.h>
#include <vector>
#include <padscore/wpad.h>
#include <padscore/kpad.h>
#include <vpad/input.h>
#include <coreinit/title.h>
#include "padscore_extra.h"

WUPS_PLUGIN_NAME("gamepadtopro");
WUPS_PLUGIN_DESCRIPTION("Maps Wii U GamePad as a Pro Controller");
WUPS_PLUGIN_VERSION("v1.0");
WUPS_PLUGIN_AUTHOR("capitalistspz");
WUPS_PLUGIN_LICENSE("MIT");

#define ENABLE_CONFIG_ID "enable"
#define CHANNEL_CONFIG_ID "channel"

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("gamepadtopro");

//! Settings
bool enablePlugin = false;
int kpadChan = WPAD_CHAN_0;

//! Info
VPADChan vpadChan = VPAD_CHAN_0;
uint64_t applicationTitleId = 0;
bool configMenuOpen = false;

bool IsGame(uint64_t id) {
    return (id & 0xFFFFFFFF'00000000ULL) == 0x00050000'00000000ULL;
}

bool UseReal() {
    return !enablePlugin || !IsGame(applicationTitleId) || configMenuOpen || !WPADIsEnabledURC();
}

void channelItemChanged(ConfigItemIntegerRange *item, int newValue) {
    if (std::string_view(CHANNEL_CONFIG_ID) == item->identifier) {
        kpadChan = (KPADChan) newValue;
        WUPSStorageAPI::Store(item->identifier, newValue);
    }
}

void enablePluginItemChanged(ConfigItemBoolean *item, bool newValue) {
    if (std::string_view(ENABLE_CONFIG_ID) == item->identifier) {
        enablePlugin = newValue;
        WUPSStorageAPI::Store(item->identifier, newValue);
    }
}

WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle) {
    configMenuOpen = true;
    // To use the C++ API, we create new WUPSConfigCategory from the root handle!
    WUPSConfigCategory root = WUPSConfigCategory(rootHandle);

    try {
        root.add(WUPSConfigItemBoolean::Create(ENABLE_CONFIG_ID, "Enable",
                                               false, enablePlugin,
                                               &enablePluginItemChanged));

        root.add(WUPSConfigItemIntegerRange::Create(CHANNEL_CONFIG_ID, "Channel",
                                                    0, kpadChan,
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

    WUPSStorageAPI::GetOrStoreDefault(ENABLE_CONFIG_ID, enablePlugin, false);
    WUPSStorageAPI::GetOrStoreDefault(CHANNEL_CONFIG_ID, kpadChan, 0);
    WUPSStorageAPI::SaveStorage();
}

ON_APPLICATION_START() {
    applicationTitleId = OSGetTitleID();
}

ON_APPLICATION_REQUESTS_EXIT() {
    applicationTitleId = 0;
}

DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffers, uint32_t count, VPADReadError *outError) {
    if (vpadChan != chan || UseReal())
        return real_VPADRead(chan, buffers, count, outError);
    bzero(buffers, sizeof(VPADStatus) * count);
    *outError = VPAD_READ_NO_SAMPLES;
    return 0;
}

DECL_FUNCTION(int32_t, KPADReadEx, KPADChan channel, KPADStatus *data, uint32_t size, KPADError *outError) {
    if (channel != kpadChan || UseReal())
        return real_KPADReadEx(channel, data, size, outError);
    if (data == nullptr || size == 0) {
        *outError = KPAD_ERROR_NO_SAMPLES;
        return 0;
    }

    std::vector<VPADStatus> buffers(size);
    VPADReadError error;
    const auto count = real_VPADRead(vpadChan, buffers.data(), size, &error);
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


#define COPY_BTNK(dest_btn, src_btn) kpad.pro.hold |= (vpad.hold & src_btn) ? dest_btn : 0; \
kpad.pro.trigger |= (vpad.trigger & src_btn) ? dest_btn : 0;                                         \
kpad.pro.release |= (vpad.release & src_btn) ? dest_btn : 0

    for (auto i = 0; i < count; ++i) {
        auto const &vpad = buffers[i];
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
    if (chan != kpadChan || UseReal())
        return real_WPADProbe(chan, outExtensionType);
    *outExtensionType = WPADExtensionType::WPAD_EXT_PRO_CONTROLLER;
    return 0;
}

DECL_FUNCTION(void, WPADControlMotor, WPADChan chan, BOOL enabled) {
    constexpr uint8_t pattern[15] = {0xFF, 0xFF};
    if (chan != kpadChan || UseReal())
        real_WPADControlMotor(chan, enabled);
    else if (enabled && WPADIsMotorEnabled())
        VPADControlMotor(vpadChan, (uint8_t *) &pattern, 2);
    else
        VPADStopMotor(vpadChan);

}

DECL_FUNCTION(WPADDataFormat, WPADGetDataFormat, WPADChan chan) {
    if (chan != kpadChan || UseReal())
        return real_WPADGetDataFormat(chan);
    return WPADDataFormat::WPAD_FMT_PRO_CONTROLLER;
}

DECL_FUNCTION(void, WPADRead, WPADChan chan, void *buffer) {
    if (chan != kpadChan || UseReal()) {
        real_WPADRead(chan, buffer);
        return;
    }

    VPADStatus vpadStatus;
    VPADReadError error;
    const auto count = real_VPADRead(vpadChan, &vpadStatus, 1, &error);

    auto *wpadStatus = (WPADStatusProController *) buffer;

    bzero(wpadStatus, sizeof(WPADStatusProController));

    wpadStatus->err = 0;

    if (count <= 0 || error != VPAD_READ_SUCCESS) {
        //! no controller
        wpadStatus->err = -1;
        return;
    }
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
    if (kpadChan != chan || UseReal())
        return real_WPADGetBatteryLevel(chan);
    // Full battery
    return 4;
}

WUPS_MUST_REPLACE(KPADReadEx, WUPS_LOADER_LIBRARY_PADSCORE, KPADReadEx);
WUPS_MUST_REPLACE(KPADRead, WUPS_LOADER_LIBRARY_PADSCORE, KPADRead);

WUPS_MUST_REPLACE(WPADProbe, WUPS_LOADER_LIBRARY_PADSCORE, WPADProbe);
WUPS_MUST_REPLACE(WPADRead, WUPS_LOADER_LIBRARY_PADSCORE, WPADRead);

WUPS_MUST_REPLACE(WPADControlMotor, WUPS_LOADER_LIBRARY_PADSCORE, WPADControlMotor);
WUPS_MUST_REPLACE(WPADGetDataFormat, WUPS_LOADER_LIBRARY_PADSCORE, WPADGetDataFormat);
WUPS_MUST_REPLACE(WPADGetBatteryLevel, WUPS_LOADER_LIBRARY_PADSCORE, WPADGetBatteryLevel);

WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);