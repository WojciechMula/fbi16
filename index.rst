===========================================================
         Image viewer for VGA16fb framebuffer
===========================================================

Last update: 15.10.2006


Introduction
-----------------------------------------------------------

This small utility displays images in VGA16fb framebuffer.

Kernel module switches to mode 640x480x16, which is not
supported by any program.  It is caused by weird EGA video
memory model---to put pixel(s) you have to reprogram video
card!

I wrote two utilities: one displays only b/w images, and
another that displays color images (but needs root
privileges).


fbi16
-----------------------------------------------------------

This program runs at user level mode and relies on kernel
module ``vga16fb`` settings.  Fortunatelly the module leaves
card in *write mode 3*, enables all planes, sets *bit mask*
to ``0xff``, and do not reset color.  We are able to put 8
pixels at once using the same, last used color (color of
last putted char).  Because of that ``fbi16`` can display
only b/w images.

The program displays PGM files.  To view any image use
shell script---it uses ``convert`` and ``pnmquant``
to produce b/w PGM files.


Compilation
~~~~~~~~~~~

::
		
	gcc fbi16.c -o fbi16.bin


Usage
~~~~~

::

	fbi16.bin file.pgm


Keyboard bindings
~~~~~~~~~~~~~~~~~

* ``q``, ``Ctrl-C`` --- quit
* ``a`` --- scroll image left
* ``A`` --- scroll image left (faster)
* ``a`` --- scroll image right
* ``S`` --- scroll image right (faster)
* ``w`` --- scroll image down
* ``W`` --- scroll image dewn (faster)
* ``z`` --- scroll image up
* ``Z`` --- scroll image up (faster)
* ``i`` --- negative
* ``enter`` --- refresh image

Downloads
~~~~~~~~~

* `fbi16.sh <fbi16.sh>`_  --- shell script
* `fbi16.c <fbi16.c>`_    --- source file


fbi16_2
-----------------------------------------------------------

This program needs root privileges, but can display 16 color
images.  It reads raw RGB images.  To view any image use
shell script---it uses ``convert`` to produce RGB files.

**Warning**: while scrolling image screen is flickering,
and I do not any idea how to avoid it.

While displaying user can't switching consoles.

Compilation
~~~~~~~~~~~

::
		
	gcc -O2 fbi16_2.c -o fbi16_2.bin

Flag ``-O2`` is **important** (see ``man 3 outb``).


Usage
~~~~~

::

	fbi16_2.bin width height file.rgb


Keyboard bindings
~~~~~~~~~~~~~~~~~

* ``q``, ``Ctrl-C`` --- quit
* ``a`` --- scroll image left
* ``A`` --- scroll image left (faster)
* ``a`` --- scroll image right
* ``S`` --- scroll image right (faster)
* ``w`` --- scroll image down
* ``W`` --- scroll image dewn (faster)
* ``z`` --- scroll image up
* ``Z`` --- scroll image up (faster)
* ``enter`` --- refresh image

Downloads
~~~~~~~~~

* `fbi16_2.sh <fbi16_2.sh>`_  --- shell script
* `fbi16_2.c <fbi16_2.c>`_    --- source file


Author
-----------------------------------------------------------

Wojciech Muła, wojciech_mula@poczta.onet.pl



..
	vim: tw=60 ts=4 sw=4
