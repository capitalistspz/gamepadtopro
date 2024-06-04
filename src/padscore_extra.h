#include <padscore/wpad.h>

extern "C"
{
BOOL
WPADIsMotorEnabled();

WPADDataFormat
WPADGetDataFormat(WPADChan chan);

//! Pro Controllers enabled
BOOL
WPADIsEnabledURC();

uint8_t
WPADGetBatteryLevel(WPADChan chan);
}