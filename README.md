# Signaldust Application / GUI toolkit for C++

## What is it?

This toolkit is the public version of my 5th(?) generation private application / GUI toolkit.
It is the result of several iterations of trying to simplify development of portable native
applications and plugins.

If you're looking for a fool-proof (well, it tries to be encourage correct usage, but there might
be a few rough edges and leaky abstractions), fully-featured enterprise-ready toolkit, then look elsewhere.
This toolkit is designed to make the lone-coder or a small team highly-productive by trying to
minimize any pointless boilerplate. On the other hand, it probably doesn't solve all your problems,
it just tries to do the boring stuff that you always need in a reasonably painless way.

It is retained mode. It is mainly designed to draw scalable vectors on the CPU, but has support
for compositing this with OpenGL rendering. This is probably not the toolkit you want to use for
games. The primary design principle is to try to minimize boilerplate, both internally and for
applications, but without sacrificing flexibility.

Also note that the included editor program `dusted` is not necessarily intended to be a production
quality product and does not necessarily represent the code quality expected from the rest of the
toolkit. While it is the editor I use as my day-to-day code editor, it's original function
was to exercise the toolkit for the purpose of dog-food testing.

The bulk of it is platform independent. The current platform wrappers support Windows (Win7+ at least;
XP should probably work, but hasn't been tested in a while) and macOS (builds against 10.9 frameworks,
currently tested on macOS 11 and 12, Intel or M1), 32-bit builds should work (feel free to report an issue if not;
my testing is somewhat sporadic), but development focus (especially performance) is for 64-bit versions.

## License

The following license applies to the toolkit itself:
```
/*****************************************************************************\
* Signaldust Toolkit (c) Copyright pihlaja@signaldust.com 2014-2024          *
*----------------------------------------------------------------------------*
* You can use and/or redistribute this for whatever purpose, free of charge, *
* provided that the above copyright notice and this permission notice appear *
* in all copies of the software or it's associated documentation.            *
*                                                                            *
* THIS SOFTWARE IS PROVIDED "AS-IS" WITHOUT ANY WARRANTY. USE AT YOUR OWN    *
* RISK. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE HELD LIABLE FOR ANYTHING.  *
\****************************************************************************/
```

The dependencies included in `dust/lib` (nanosvg, stb_image, stb_truetype, gl3w, sse2neon) each have
permissive licenses (eg. MIT or similar), see each file for the details. The scancode conversion tables
are from SDL (for compatibility), see license in `dust/gui/scancode.h` for details.

The default fonts included are Deja Vu Sans / Sans Mono, these have been converted into headers
(xxd dumps of the original .ttf files) so that there is always some font available to draw text.

## Building

In theory, type `make` and it will find all source files inside `dust/` and build them
into a library, then similarly find all source files in each sub-directory of `programs`
and build such subdirectory into a program placed in `bin`.

Experimental: Sub-directories of `plugins` are also compiled into shared objects in `build/`
although the details of this might change (might want to auto-bundle on macOS at least?).

This should work on macOS (targeting 10.9 and later) and Windows (with `clang` in path).
The `Makefile` includes `local.make` if such a file exists, in case you need local overrides.

If you want to build using a build-system other than my `Makefile` then pretty much the
only thing you need to worry about is defining `DUST_COCOA_PREFIX` when compiling the
macOS system wrapper `dust/gui/sys_osx.mm`. For applications this can be anything, but when
using the toolkit for plugins (eg. AudioUnit, VST) this should be unique (which is why
the `Makefile` generates a random UUID for each build by default).

## Contributing

In general, if you want to help improve some aspect of the toolkit, then I would generally
recommend opening an issue first to discuss what you want to do.

Any pull-requests must compile as C++11 and should not contain excessive long lines of source
code (aim at ~80 characters), tab-characters are strictly forbidden and `struct` (or `typename`
for templates) should be used in place of `class` whenever possible.

Why these seemingly weird conventions? Because I don't actually care.

## Where to start?

Start by taking a look at the [hello world example](programs/hello/hello.cpp).

Other than that, at this point, there isn't any documentation yet,
but the header files are quite extensively commented.

For the main toolkit, the best starting points are `dust/gui/panel.h`, `dust/gui/window.h`
and `dust/gui/app.h` and for the rendering pipeline you should start from `dust/render/render.h`.

The whole thing is designed to be fairly modular: `core` is required by most things,
the `gui` module also depends on `render` and `dust/widgets/textarea.h` depends on `regex`.

The `thread` module is optional and somewhat geared towards real-time (eg. audio) work.

That said as a general rule-of-thumb the toolkit follows the principle that whoever creates
an object/resource also owns it. For example, any `Panel` can be placed in stack, heap,
as member of another `Panel` or some helper class, where as a `Window` (created by the toolkit)
is owned by the toolkit (with lifetime generally associated with the lifetime of the underlying
OS window).

In general, any interface taking pointers or references does not transfer object ownership.
This also applies to parent-child relationships of widgets: while these internally retain references
such relationships are simply automatically disconnected if either the parent or the child is destroyed.

Because the entire widget-tree is fully dynamic and any widget (or even the entire GUI) can be
linked to many different windows over it's lifetime, the toolkit provides another useful invariant:
the event-handling methods are never called unless the widget (or one of it's current grand-parents)
is linked to some `Window` (ie. `getWindow()` will never return null inside `render` or any of the
methods with names starting with `ev_`).

In some cases, it might also be necessary to attach additional resources to objects we either can't
or don't want to extend directly. The most obvious case of this would be creating OpenGL resources
which need to be specific to a certain `Window` (since it's the window that owns the OpenGL context).
For these types of situations, the toolkit also includes a flexible "component system" that
allows one to attach "components" to any object that derives from `ComponentHost` (eg. `Window`).

Any components are created the first time you query for one and they are destroyed when the hosting
object is destroyed. In case of `Window` and OpenGL resources specifically, the toolkit also
guarantees that the correct context is active during `render`, `ev_update` and the component teardown,
as long as this teardown is initiated by the destruction of the `Window` (ie. no such guarantee is made
if the teardown happens because the relevant component manager itself is destroyed; while not strictly
necessary, it is typically a good idea to just make your component managers global if possible).
