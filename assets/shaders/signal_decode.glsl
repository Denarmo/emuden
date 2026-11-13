// A television taking the signal apart again. Colour is found by multiplying with the
// carrier and averaging over whole turns of it, so it is always softer than brightness and
// bleeds sideways. On one wire (p_crosstalk 1, composite) brightness and colour cannot be
// told apart completely: colour that stays in the brightness is the crawling dots, fine
// detail taken for colour is the rainbow fringe. On two wires (0, S-Video) neither happens.
uniform float p_crosstalk;
uniform float p_crawl;
uniform float p_dots;
uniform float p_fringe;
uniform float p_bleed;
uniform float p_color;

// A window reaching `reach` samples each side; it is blind to the carrier when that is a
// whole number of turns (4 samples a turn).
float Window(float k, float reach) {
    return abs(k) < reach ? 0.5 + 0.5 * cos(PI * k / reach) : 0.0;
}

void main() {
    vec2 size = u_source_size;
    vec2 cell = floor(v_uv * size);
    float sharp = 0.0;
    float sharp_total = 0.0;
    float notch = 0.0;
    float notch_total = 0.0;
    vec2 close = vec2(0.0);
    vec2 wide = vec2(0.0);
    float wide_total = 0.0;
    for (int n = -8; n <= 8; n++) {
        float k = float(n);
        vec2 at = cell + vec2(k, 0.0);
        vec4 s = Source((at + 0.5) / size);
        float luma = s.r;
        float chroma = (s.g - 0.5) * 2.0;
        float phase = CarrierPhase(at, p_crawl);
        vec2 carrier = vec2(cos(phase), sin(phase));
        float on_luma = luma + p_crosstalk * chroma;
        float on_chroma = chroma + p_crosstalk * p_fringe * luma;

        float w = Window(k, 2.0);
        sharp += w * on_luma;
        sharp_total += w;
        w = Window(k, 4.0);
        notch += w * on_luma;
        notch_total += w;
        close += w * on_chroma * carrier;
        w = Window(k, 8.0);
        wide += w * on_chroma * carrier;
        wide_total += w;
    }
    float y = mix(notch / notch_total, sharp / sharp_total, mix(1.0, p_dots, p_crosstalk));
    vec2 iq = 2.0 * mix(close / notch_total, wide / wide_total, p_bleed) * p_color;
    o_color = vec4(clamp(FromYiq(vec3(y, iq)), vec3(0.0), vec3(1.0)), 1.0);
}
