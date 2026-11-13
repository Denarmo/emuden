// Dark lines between the picture's rows, nothing else.
uniform float p_lines;
uniform float p_bright;

void main() {
    vec3 c = ToLinear(Source(SharpCoord(v_uv, u_source_size, u_output_size)).rgb);
    float rows = u_original_size.y;
    // Under about three output pixels a row there is no room for a line.
    float room = clamp((u_output_size.y / rows - 1.5) / 1.5, 0.0, 1.0);
    float lit = 0.5 - 0.5 * cos(2.0 * PI * v_uv.y * rows);
    float dark = p_lines * room;
    c *= mix(1.0, lit, dark) * mix(1.0, p_bright, dark);
    o_color = vec4(ToGamma(c), 1.0);
}
