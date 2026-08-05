/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_adc.h"

#include "hardware/adc.h"




/***************************************************************************************
**
** Compiler Define Directives
**
***************************************************************************************/


/* The temperature sensor is the last ADC input, and the SDK constant tracks
   which one that is per package: 4 on the RP2350A built today, 8 on an
   RP2350B. It is on-die, so selecting it drives no pad -- which matters here,
   because where the analog-capable GPIOs go on this board is not documented
   anywhere and RP2350-E9 makes a floating input worse than an unused one. A
   hardcoded 4 would quietly convert a pad's voltage on a B-package retarget. */
#define ADC_TEMPERATURE_INPUT ( (uint32_t) ADC_TEMPERATURE_CHANNEL_NUM )


/* Datasheet transfer function, in integer milli-units so no float is linked:
   T = 27 - (V - 0.706) / 0.001721, with V = raw * 3.3 / 4096. */
#define ADC_REFERENCE_MICROVOLTS ( (int32_t) 3300000 )
#define ADC_FULL_SCALE ( (int32_t) 4096 )
#define ADC_SENSOR_OFFSET_MICROVOLTS ( (int32_t) 706000 )
#define ADC_SENSOR_SLOPE_MICROVOLTS_PER_DEGREE ( (int32_t) 1721 )
#define ADC_SENSOR_OFFSET_MILLI_CELSIUS ( (int32_t) 27000 )
#define MILLI_PER_UNIT ( (int32_t) 1000 )




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Powers the converter and its temperature sensor. Separate from the first
///     reading because the sensor needs the bias to settle, and a boot-time init
///     gives it that for free rather than paying for it inside a test step.
/// </summary>
void BSP_AdcInit( void )
{
    adc_init();
    adc_set_temp_sensor_enabled( true );
}


/// <summary>
///     Converts one sample from the on-die sensor. Absolute accuracy is poor --
///     the datasheet allows several degrees without calibration -- so this is
///     worth testing as a band rather than a value: a reading pinned at zero or
///     full scale means the converter or its reference is dead, which is the
///     failure actually worth catching.
/// </summary>
/// <returns>
///     The raw conversion result alongside the converted temperature in
///     thousandths of a degree Celsius.
/// </returns>
bsp_adc_temperature_t BSP_AdcTemperature( void )
{
    adc_select_input( ADC_TEMPERATURE_INPUT );

    /* Widened deliberately. A mid-scale count times a three-million microvolt
       reference is about 2.9e9, which overflows a signed 32-bit intermediate and
       comes back as a plausible-looking temperature in the hundreds of degrees
       rather than as an obvious error. The same is true of the slope division,
       so both stay 64-bit and only the result narrows. */
    const int32_t raw = (int32_t) adc_read();
    const int32_t microvolts =
        (int32_t) ( ( (int64_t) raw * ADC_REFERENCE_MICROVOLTS ) / ADC_FULL_SCALE );
    const int32_t aboveOffset = microvolts - ADC_SENSOR_OFFSET_MICROVOLTS;
    const bsp_adc_temperature_t sample = {
        .raw = (uint16_t) raw,
        .milli_celsius = ADC_SENSOR_OFFSET_MILLI_CELSIUS -
                         (int32_t) ( ( (int64_t) aboveOffset * MILLI_PER_UNIT ) /
                                     ADC_SENSOR_SLOPE_MICROVOLTS_PER_DEGREE ),
    };
    return sample;
}
