# greg

**greg** is a code generator that produces an OpenGL core and extension function
API loader library and assocaiated header, tailored to your needs.  It is
lovingly sculpted from a block of pure frustration with the large number of
promising and yet ultimately unusable loader generators out there.

The primary sources of inspiration are [flextGL](https://github.com/ginkgo/flextGL)
and [glad](https://github.com/Dav1dde/glad), in that order.

greg is licensed under the 
[zlib/libpng license](http://opensource.org/licenses/Zlib), which can be found
in the `COPYING.md` file.  The license is also included at the top of the source
file.

greg is **not yet done**.  Go away.  Come back when it's done.


## FAQ

### What's with the name?

**glelg** (i.e. OpenGL Extension Loader Generator) sounds stupid, whereas
**greg** sounds stupid in an entirely different way.  If you want, you can
pretend that greg stands for オープンジーエルエクステンションジェネレータ.

### Why is this written in C++ instead of Python.canvas.nodesharp?

Here is the [Wikipedia page for lettuce](https://en.wikipedia.org/wiki/Lettuce).


## Dependencies

greg requires a C++11 compiler, which is what you're going to be compiling its
output with anyway, [CMake](http://www.cmake.org/) for generating project or
make files, and comes already bundled with its remaining dependencies:

 - [getopt\_port](https://github.com/kimgr/getopt_port/) for parsing
   command-line options
 - [pugixml](http://pugixml.org/) for a wonderful little XML DOM with XPath
 - [wire](https://github.com/r-lyeh/wire) for a usable replacement string class

