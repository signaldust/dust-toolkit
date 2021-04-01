# Signaldust Application / GUI toolkit for C++

## What is it?

This toolkit is the public version of my 5th(?) generation private application / GUI toolkit.
It is the result of several iterations of trying to simplify development of portable native
applications and plugins.

If you're looking for a fool-proof, fully-featured enterprise-ready toolkit, then look elsewhere.
This toolkit is designed to make the lone-coder or a small team highly-productive by trying to
minimize any pointless boilerplate.

It is retained mode. It is mainly designed to draw scalable vectors on the CPU, but has support
for composising this with OpenGL rendering. This is probably not the toolkit you want to use for
games. The primary design principle is to try to minimize boilerplate, both internally and for
applications, but without sacrificing flexibility.

## License

The following license applies to the toolkit itself:
```
/*****************************************************************************\
* Signaldust Toolkit (c) Copyright pihlaja@signaldust.com 2014-2021          *
*----------------------------------------------------------------------------*
* You can use and/or redistribute this for whatever purpose, free of charge, *
* provided that the above copyright notice and this permission notice appear *
* in all copies of the software or it's associated documentation.            *
*                                                                            *
* THIS SOFTWARE IS PROVIDED "AS-IS" WITHOUT ANY WARRANTY. USE AT YOUR OWN    *
* RISK. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE HELD LIABLE FOR ANYTHING.  *
\****************************************************************************/
```

The dependencies included in `dust/lib` (nanovg, picopng, stb_truetype) each have
permissive license, see each file for the details.

## Building

In theory, type `make` and it will find all source files inside `dust/` and build them
into a library, then similarly find all source files in each sub-directory of `programs`
and build them into programs.

This should work on macOS (targeting 10.9) and Windows (with `clang` in path).
The `Makefile` includes `local.make` if such a file exists, in case you need local overrides.

If you want to build using a build-system other than my `Makefile` then pretty much the
only thing you need to worry about is defining `DUST_COCOA_PREFIX` when compiling the
macOS system wrapper `dust/gui/sys_osx.mm`. For applications this can be anything, but when
using the toolkit for plugins (eg. AudioUnit, VST) this should be unique (which is why
the `Makefile` generates a random UUID for each build by default).

## Where to start?

At this point, there isn't any documentation yet, other than the header files themselves.

That said as a general rule-of-thumb the toolkit follows the principle that whoever creates
an object/resource also owns it. For example, any `Control` can be placed in stack, heap,
as member of another `Control` or some helper class. 

For the main toolkit, the best starting points are `dust/gui/control.h`, `dust/gui/window.h`
and `dust/gui/app.h` and for the rendering pipeline you should start from `dust/render/render.h`.

The whole thing is designed to be fairly modular: `core` is required by most things,
the `gui` module also depends on `render` and `dust/widgets/textarea.h` depends on `regex`.

The `thread` module is optional and somewhat geared towards real-time (eg. audio) work.

## Contributing

In general, if you want to help improve some aspect of the toolkit, then I would generally
recommend opening an issue first to discuss what you want to do.

Any pull-requests must compile as C++11 and should not contain excessive long lines of source
code (aim at ~80 characters), tab-characters are strictly forbidden and `struct` should be
used in place of `class` whenever possible.
