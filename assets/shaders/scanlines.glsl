// Dark lines between the picture's rows, nothing else.
uniform float p_lines;
uniform float p_bright;

void main() {
    vec3 c = ToLinear(Source(SharpCoord(v_uv, u_source_size, u_output_size)).rgb);
    c *= mix(1.0, RowLight(v_uv.y * u_original_size.y, u_output_size.y / u_original_size.y), p_lines) *
         mix(1.0, p_bright, p_lines);
    o_color = vec4(ToGamma(c), 1.0);
}
