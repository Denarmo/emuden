// A slow LCD: every pixel keeps part of what it showed before.
uniform float p_ghost;

void main() {
    vec3 now = Source(v_uv).rgb;
    vec3 before = History(v_uv).rgb;
    o_color = vec4(mix(now, before, clamp(p_ghost, 0.0, 0.95)), 1.0);
}
