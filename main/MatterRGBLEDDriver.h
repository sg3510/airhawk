#include <esp_err.h>

class MatterRGBLEDDriver
{

    public:

        static MatterRGBLEDDriver* Create();

        esp_err_t SetPower(bool onOff);

        esp_err_t SetBrightness(uint8_t matterBrightness);

        esp_err_t SetHue(uint8_t matterHue);

        esp_err_t SetSaturation(uint8_t matterSaturation);

        esp_err_t SetTemperature(uint16_t matterSaturation);

    private:

        MatterRGBLEDDriver(void* lightHandle);

        void* m_lightHandle = nullptr;
    
};