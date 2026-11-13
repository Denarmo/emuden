"""Compiles every pass of every picture preset as GLSL 3.30 and as GLSL ES 3.00, the way
gfx::EffectChain puts a pass together.

    python player/tools/check_shaders.py [--glslc <path>] [--angle <folder with libEGL>]

GLSL 3.30 goes through glslc (Vulkan SDK or Android NDK shader-tools). glslc does not take
ES 3.00, so that goes through an OpenGL ES 3 context of ANGLE (libEGL and libGLESv2, which
Chromium-based programs ship) when one is found; without it the passes are compiled as
ES 3.10 by glslc, which is close but not the same language.

Exits with 1 when a pass does not compile.
"""
import argparse
import ctypes
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SHADERS = os.path.join(ROOT, 'assets', 'shaders')
HEADERS = {
    'GLSL 3.30': '#version 330 core\n',
    'GLSL ES 3.00': '#version 300 es\nprecision highp float;\nprecision mediump sampler2D;\n',
}


def find_glslc(given):
    if given:
        return given
    found = shutil.which('glslc')
    if found:
        return found
    for base in (os.environ.get('ANDROID_NDK_HOME'), os.environ.get('ANDROID_HOME'),
                 os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Android', 'Sdk')):
        if base:
            hits = sorted(glob.glob(os.path.join(base, '**', 'shader-tools', '*', 'glslc*'), recursive=True))
            if hits:
                return hits[-1]
    return None


def find_angle(given):
    places = [given] if given else []
    for base in (os.environ.get('LOCALAPPDATA'), os.environ.get('ProgramFiles'), os.environ.get('ProgramFiles(x86)')):
        for depth in range(2, 6):
            if base:
                places += glob.glob(os.path.join(base, *['*'] * depth, 'libEGL.dll'))
    for place in places:
        folder = place if os.path.isdir(place) else os.path.dirname(place)
        if all(os.path.isfile(os.path.join(folder, name)) for name in ('libEGL.dll', 'libGLESv2.dll')):
            return folder
    return None


class Angle:
    """An OpenGL ES 3 context that only compiles fragment shaders."""

    def __init__(self, folder):
        os.add_dll_directory(folder)
        self.egl = ctypes.CDLL(os.path.join(folder, 'libEGL.dll'))
        self.gl = ctypes.CDLL(os.path.join(folder, 'libGLESv2.dll'))
        egl, pointer = self.egl, ctypes.c_void_p
        egl.eglGetDisplay.restype = pointer
        egl.eglGetDisplay.argtypes = [pointer]
        egl.eglInitialize.argtypes = [pointer, pointer, pointer]
        egl.eglChooseConfig.argtypes = [pointer, pointer, pointer, ctypes.c_int, pointer]
        egl.eglCreatePbufferSurface.restype = pointer
        egl.eglCreatePbufferSurface.argtypes = [pointer, pointer, pointer]
        egl.eglCreateContext.restype = pointer
        egl.eglCreateContext.argtypes = [pointer, pointer, pointer, pointer]
        egl.eglMakeCurrent.argtypes = [pointer] * 4

        def ints(*values):
            return (ctypes.c_int * len(values))(*values)

        display = egl.eglGetDisplay(None)
        if not display or not egl.eglInitialize(display, None, None):
            raise RuntimeError('no EGL display')
        config = pointer()
        count = ctypes.c_int()
        # EGL_SURFACE_TYPE = PBUFFER, EGL_RENDERABLE_TYPE = OPENGL_ES3
        egl.eglChooseConfig(display, ints(0x3033, 0x0001, 0x3040, 0x0040, 0x3038), ctypes.byref(config), 1,
                            ctypes.byref(count))
        if count.value < 1:
            raise RuntimeError('no EGL config for OpenGL ES 3')
        surface = egl.eglCreatePbufferSurface(display, config, ints(0x3057, 16, 0x3056, 16, 0x3038))
        context = egl.eglCreateContext(display, config, None, ints(0x3098, 3, 0x3038))
        if not context or not egl.eglMakeCurrent(display, surface, surface, context):
            raise RuntimeError('no OpenGL ES 3 context')
        self.gl.glGetString.restype = ctypes.c_char_p
        self.version = self.gl.glGetString(0x1F02).decode()

    def compile(self, source):
        gl = self.gl
        shader = gl.glCreateShader(0x8B30)
        text = ctypes.c_char_p(source.encode())
        gl.glShaderSource(shader, 1, ctypes.byref(text), None)
        gl.glCompileShader(shader)
        status = ctypes.c_int()
        gl.glGetShaderiv(shader, 0x8B81, ctypes.byref(status))
        log = ctypes.create_string_buffer(8192)
        gl.glGetShaderInfoLog(shader, 8192, None, log)
        gl.glDeleteShader(shader)
        return status.value != 0, log.value.decode(errors='replace')


def declarations():
    text = open(os.path.join(ROOT, 'gfx', 'src', 'effect_chain.cpp'), encoding='utf-8').read()
    return re.search(r'DECLARATIONS = R"\((.*?)\)";', text, re.S).group(1)


def with_glslc(glslc, work, source):
    file = os.path.join(work, 'pass.frag')
    open(file, 'w', encoding='utf-8', newline='\n').write(source)
    run = subprocess.run([glslc, '-fshader-stage=frag', '--target-env=opengl', '-fauto-map-locations',
                          '-fauto-bind-uniforms', '-Werror', '-o', os.path.join(work, 'pass.spv'), file],
                         capture_output=True, text=True)
    return run.returncode == 0, run.stdout + run.stderr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--glslc')
    parser.add_argument('--angle')
    args = parser.parse_args()
    glslc = find_glslc(args.glslc)
    if not glslc:
        print('glslc not found: pass --glslc')
        return 2
    angle = None
    folder = find_angle(args.angle)
    if folder:
        try:
            angle = Angle(folder)
            print(f'GLSL ES 3.00 through ANGLE: {angle.version}')
        except (OSError, RuntimeError) as error:
            print(f'ANGLE in {folder} does not start ({error})')
    if not angle:
        print('no ANGLE found: the ES passes are compiled as ES 3.10 by glslc')
    presets = json.load(open(os.path.join(SHADERS, 'presets.json'), encoding='utf-8'))['presets']
    common = open(os.path.join(SHADERS, 'common.glsl'), encoding='utf-8').read()
    passes = set()
    for preset in presets:
        for item in preset['passes']:
            passes.add((item['shader'], tuple(item.get('defines', []))))
    failed = 0
    with tempfile.TemporaryDirectory() as work:
        for shader, defines in sorted(passes):
            body = open(os.path.join(SHADERS, shader + '.glsl'), encoding='utf-8').read()
            source = declarations() + ''.join(f'#define {d} 1\n' for d in defines) + common + '\n' + body
            label = shader + (' [' + ' '.join(defines) + ']' if defines else '')
            for name, header in HEADERS.items():
                if 'ES' in name and angle:
                    good, log = angle.compile(header + source)
                elif 'ES' in name:
                    good, log = with_glslc(glslc, work, header.replace('300 es', '310 es') + source)
                else:
                    good, log = with_glslc(glslc, work, header + source)
                if not good:
                    failed += 1
                print(f'{"ok  " if good else "FAIL"} {label} as {name}' + ('' if good else '\n' + log))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
