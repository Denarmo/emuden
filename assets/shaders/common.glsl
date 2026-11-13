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

// The light of a row of the picture at `y` (in rows), between 0 in the dark line between
// two rows and 1 in a row's middle, as an output pixel averages it when a row is `room`
// output pixels high. The average is a half at any size: where there is no room for lines
// (under about three pixels a row, sooner when rows and pixels do not line up) they fade
// into it instead of beating against the pixels.
float RowLight(float y, float room) {
    float narrow = PI / max(room, 1.0);
    float uneven = abs(room - floor(room + 0.5)) * 2.0;
    float fade = clamp((room - 1.5) / 1.5, 0.0, 1.0) * mix(1.0, clamp((room - 2.0) / 2.0, 0.0, 1.0), uneven);
    return 0.5 - 0.5 * cos(2.0 * PI * y) * fade * sin(narrow) / narrow;
}

// How wide the stretch of the picture is that an output pixel averages, in the picture's
// pixels, when one of them takes `room` output pixels. It is the output pixel itself where
// the two line up; where they do not, up to half as much again, which keeps lines that
// fall differently on the pixels from looking different.
vec2 Footprint(vec2 room) {
    vec2 uneven = abs(room - floor(room + 0.5)) * 2.0;
    return min((1.0 + 0.5 * uneven) / room, vec2(1.0));
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
