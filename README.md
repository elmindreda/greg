# GREG

**GREG** is a code generator that produces an stb-style header-only OpenGL and
OpenGL ES function loader library tailored to your needs.  It is lovingly
sculpted from a block of pure frustration with the large number of promising and
yet ultimately unusable loader generators out there.

The primary sources of inspiration are [flextGL](https://github.com/ginkgo/flextGL)
and [glad](https://github.com/Dav1dde/glad), in that order.

GREG is licensed under the 
[zlib/libpng license](http://opensource.org/licenses/Zlib), which can be found
in the `COPYING.md` file.  The license is also included at the top of the source
file.

GREG is **not yet done**.  Go away.  Shoo.  Come back when it's done.


## Tutorial

Compile and run `greg` to generate the desired `greg.h` header library.  See
`greg --help` for details.

Include `greg.h` where needed.  Define `GREG_IMPLEMENTATION` before inclusion in
exactly one compilation unit.

Get a current OpenGL or OpenGL ES context somehow.  Call `gregInit`.  If it
returns non-zero, you're done.  If it returns zero something is broken and
you're out of luck.


## Backend selection

GREG supports loading via native APIs on Windows, OS X and systems running X11,
and will auto-select the proper backend at compile-time.  GREG also supports
loading via EGL, GLFW 3 and SDL 2 by defining `GREG_USE_EGL`, `GREG_USE_GLFW3`
or `GREG_USE_SDL2`, respectively.


## FAQ

### What's with the name?

**GLELG** (i.e. OpenGL Extension Loader Generator) sounds stupid, whereas
**GREG** sounds stupid in a different way.  If you want, you can pretend that
GREG stands for オープンジーエルエクステンションジェネレータ.  I do that
sometimes.

### Why is this written in C++ instead of Python.canvas.nodesharp?

Here is the [Wikipedia page for lettuce](https://en.wikipedia.org/wiki/Lettuce).

### Why is there no support for [GLWT](https://github.com/rikusalminen/glwt)?

GLWT does not provide the functions needed to allow GREG to load through it, but
GLWT will co-exist just fine with the native backends.

### Why is there no support for GLUT?

Here is the [Wikipedia page for yarn](https://en.wikipedia.org/wiki/Yarn).


## Dependencies

GREG requires a C++11 compiler, which is probably what you're going to be
compiling its output with anyway, [CMake](http://www.cmake.org/) for generating
project or make files, and comes already bundled with its remaining
dependencies.  These are:

 - [getopt\_port](https://github.com/kimgr/getopt_port/) for parsing
   command-line options
 - [pugixml](http://pugixml.org/) for a wonderful little XML DOM with XPath
 - [wire](https://github.com/r-lyeh/wire) for a usable replacement string class

The *output* of GREG requires only a C89 compiler and headers for the selected
backend.

GREG has been successfully built with the following C++11 compilers:

 - Clang 3.0 with libstdc++
 - Visual C++ 12.0 (VS 2013)
 - GCC 4.9.2 cygstdc++

