// One direction of a soft blur, in linear light. Two of these (HORIZONTAL, then not) on a
// small target give the glow the bloom passes add.
void main() {
#ifdef HORIZONTAL
    vec2 dir = vec2(1.5 / u_output_size.x, 0.0);
#else
    vec2 dir = vec2(0.0, 1.5 / u_output_size.y);
#endif
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int i = -4; i <= 4; i++) {
        float k = float(i);
        float w = exp(-k * k / 8.0);
        sum += w * ToLinear(Source(v_uv + dir * k).rgb);
        total += w;
    }
    o_color = vec4(ToGamma(sum / total), 1.0);
}
