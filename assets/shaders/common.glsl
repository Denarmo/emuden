// Shared by every pass of the picture effects. Written for EmuDEN; GLSL that GL 3.3 and
// GLES 3.0 both take: no implicit int to float, no arrays of samplers, loops with constant
// bounds.

const float PI = 3.14159265;

vec3 ToLinear(vec3 c) {
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

vec3 ToGamma(vec3 c) {
    return pow(clamp(c, vec3(0.0), vec3(1.0)), vec3(1.0 / 2.2));
}

float Luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// A number in 0..1 from two whole numbers (a pixel, a line and a frame).
float Hash(vec2 p) {
    uint n = uint(int(p.x)) * 2654435761u + uint(int(p.y)) * 2246822519u;
    n ^= n >> 15;
    n *= 739982445u;
    n ^= n >> 12;
    n *= 695872825u;
    n ^= n >> 15;
    return float(n >> 8) * (1.0 / 16777216.0);
}

// Where to take a linear sample so that pixels stay sharp: it only blends across the last
// output pixel between two source pixels.
vec2 SharpCoord(vec2 p, vec2 size, vec2 shown) {
    vec2 t = p * size;
    vec2 scale = max(floor(shown / size), vec2(1.0));
    return (floor(t) + 0.5 + clamp((fract(t) - 0.5) * scale, vec2(-0.5), vec2(0.5))) / size;
}

// Colour as a television signal carries it: brightness and two colour differences.
vec3 ToYiq(vec3 c) {
    return vec3(dot(c, vec3(0.299, 0.587, 0.114)), dot(c, vec3(0.596, -0.274, -0.322)),
                dot(c, vec3(0.211, -0.523, 0.312)));
}

vec3 FromYiq(vec3 c) {
    return vec3(c.x + 0.956 * c.y + 0.621 * c.z, c.x - 0.272 * c.y - 0.647 * c.z,
                c.x - 1.106 * c.y + 1.703 * c.z);
}

// The colour carrier's angle at a signal sample: a quarter turn per sample, a third of a
// turn more on every line, and (with `crawl`) on every frame, which is what makes the dots
// of a composite picture walk.
float CarrierPhase(vec2 cell, float crawl) {
    float turn = cell.x * 0.25 + mod(cell.y, 3.0) / 3.0 + crawl * mod(u_frame, 3.0) / 3.0;
    return 2.0 * PI * fract(turn);
}
