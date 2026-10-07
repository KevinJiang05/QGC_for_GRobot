void MAIN()
{
    float ripple = sin(UV0.x * 72.0 + wavePhase)
                 * sin(UV0.y * 58.0 - wavePhase * 0.7);
    BASE_COLOR = vec4(waterColor.rgb * (0.94 + ripple * 0.06), waterColor.a);
    ROUGHNESS = 0.26;
    METALNESS = 0.0;
}
