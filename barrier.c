/*
 * Based on Peter Hutterer's example:
 *
 * http://who-t.blogspot.de/2012/12/whats-new-in-xi-23-pointer-barrier.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

int
main(int argc, char **argv)
{
	if (argc != 5)
	{
		fprintf(stderr, "Usage: %s <x1> <y1> <x2> <y2>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	Display *dpy = XOpenDisplay(NULL);
	int fixes_opcode, fixes_event_base, fixes_error_base;

	if (!XQueryExtension(dpy, "XFIXES", &fixes_opcode, &fixes_event_base,
	    &fixes_error_base))
	{
		fprintf(stderr, "No XFIXES extension available.\n");
		exit(EXIT_FAILURE);
	}

	XFixesCreatePointerBarrier(dpy, DefaultRootWindow(dpy),
			atoi(argv[1]), atoi(argv[2]),
			atoi(argv[3]), atoi(argv[4]),
			0,         /* block in all directions */
			0, NULL);  /* no per-device barriers */

	XEvent ev;
	XSync(dpy, False);
	while (!XNextEvent(dpy, &ev))
		/* nothing */ ;

	exit(EXIT_SUCCESS);
}
