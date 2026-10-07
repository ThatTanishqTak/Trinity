// The sRGB transfer function both ways, per channel, as ToneMapping.cpp has it on the CPU
float SrgbToLinear(float value)
{
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

float3 SrgbToLinear(float3 color)
{
    return float3(SrgbToLinear(color.r), SrgbToLinear(color.g), SrgbToLinear(color.b));
}

float LinearToSrgb(float value)
{
    return value <= 0.0031308 ? value * 12.92 : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

float3 LinearToSrgb(float3 color)
{
    return float3(LinearToSrgb(color.r), LinearToSrgb(color.g), LinearToSrgb(color.b));
}