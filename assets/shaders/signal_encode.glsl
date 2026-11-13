// The picture as a video signal, two samples a pixel: brightness in red, and in green the
// two colour differences riding on the colour carrier (0.5 is no colour).
uniform float p_crawl;

void main() {
    vec3 yiq = ToYiq(Source(v_uv).rgb);
    float phase = CarrierPhase(floor(v_uv * u_output_size), p_crawl);
    float chroma = yiq.y * cos(phase) + yiq.z * sin(phase);
    o_color = vec4(yiq.x, chroma * 0.5 + 0.5, 0.0, 1.0);
}
