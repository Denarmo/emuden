// A worn video tape, at the picture's own size: lines that do not start quite where they
// should, a band of tracking noise wandering up the picture, the torn lines at the bottom
// where the heads switch, colour that is smeared and late, grain and drop-outs.
uniform float p_tracking;
uniform float p_smear;
uniform float p_noise;
uniform float p_wobble;

void main() {
    vec2 size = u_output_size;
    vec2 pixel = floor(v_uv * size);
    float line = pixel.y;
    float t = u_frame;

    // The band takes about seven seconds to cross the picture.
    float band_at = fract(t / 420.0 + 0.35);
    float away = abs(v_uv.y - band_at);
    away = min(away, 1.0 - away);
    float band = p_tracking * (1.0 - smoothstep(0.0, 0.05, away));
    float head = smoothstep(0.955, 0.985, v_uv.y);

    float shift = (Hash(vec2(line, t)) - 0.5) * 0.7 * p_wobble;
    shift += sin(line * 0.09 + t * 0.07) * 0.4 * p_wobble;
    shift += band * (Hash(vec2(line, t + 61.0)) - 0.35) * 14.0;
    shift += head * (10.0 + 22.0 * Hash(vec2(line, t + 113.0)));
    vec2 p = vec2(v_uv.x + shift / size.x, v_uv.y);
    vec2 px = 1.0 / size;

    // Brightness loses a little detail; colour loses most of it and arrives late.
    float y = 0.0;
    for (int i = -1; i <= 1; i++) {
        y += (i == 0 ? 0.5 : 0.25) * ToYiq(Source(p + vec2(float(i) * px.x, 0.0)).rgb).x;
    }
    vec2 iq = vec2(0.0);
    float total = 0.0;
    float reach = 1.0 + 3.0 * p_smear;
    for (int i = 0; i < 7; i++) {
        float k = float(i);
        float w = exp(-k * 0.45);
        vec2 at = p - vec2(k * reach * px.x * 0.6, 0.0);
        iq += w * (ToYiq(Source(at).rgb).yz + ToYiq(Source(at - vec2(0.0, px.y)).rgb).yz);
        total += 2.0 * w;
    }
    iq = iq / total * (0.9 - 0.5 * head);

    y = y * 0.93 + 0.035;
    y += (Hash(pixel + vec2(37.0, 91.0) * t) - 0.5) * 0.14 * p_noise;
    // Drop-outs: short white dashes, a few anywhere and many inside the noise.
    vec2 dash = vec2(floor((pixel.x + 40.0 * Hash(vec2(line, t + 7.0))) / 20.0), line);
    float chance = 0.0015 * p_noise + 0.1 * band + 0.15 * head;
    float hit = step(1.0 - chance, Hash(dash + vec2(13.0, 57.0) * t));
    y = mix(y, 0.85 + 0.15 * Hash(pixel + vec2(t)), hit);
    iq *= 1.0 - hit;
    o_color = vec4(clamp(FromYiq(vec3(y, iq)), vec3(0.0), vec3(1.0)), 1.0);
}
