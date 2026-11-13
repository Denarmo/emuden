// Sharp bilinear: whole pixels, with the edges between them smoothed over one output pixel.
void main() {
    o_color = vec4(Source(SharpCoord(v_uv, u_source_size, u_output_size)).rgb, 1.0);
}
