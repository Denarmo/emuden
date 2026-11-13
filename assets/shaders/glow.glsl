// A television in a dark room: the picture as it is, with light spilling from its bright
// parts. Source is the blurred picture, Original the sharp one.
uniform float p_glow;
uniform float p_lines;
uniform float p_vignette;

void main() {
    vec3 c = ToLinear(Original(SharpCoord(v_uv, u_original_size, u_output_size)).rgb);
    vec3 soft = ToLinear(Source(v_uv).rgb);
    c += p_glow * soft * (0.25 + 1.5 * Luma(soft));
    c *= mix(1.0, RowLight(v_uv.y * u_original_size.y, u_output_size.y / u_original_size.y), p_lines);
    vec2 q = v_uv * 2.0 - 1.0;
    c *= 1.0 - p_vignette * 0.3 * dot(q, q);
    // Bright parts roll off instead of clipping.
    c = c / (1.0 + max(c - 0.8, vec3(0.0)));
    o_color = vec4(ToGamma(c), 1.0);
}
