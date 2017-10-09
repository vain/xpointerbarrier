/*
 * Based on Peter Hutterer's example:
 *
 * http://who-t.blogspot.de/2012/12/whats-new-in-xi-23-pointer-barrier.html
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xlib.h>

struct Insets
{
    int top, left, right, bottom;
};

int do_toggle = 0;
int verbose = 0;

PointerBarrier
create_barrier_verbose(Display *dpy, Window w, int x1, int y1,
                       int x2, int y2, int directions,
                       int num_devices, int *devices)
{
    PointerBarrier b;

    b = XFixesCreatePointerBarrier(dpy, w, x1, y1, x2, y2, directions,
                                   num_devices, devices);

    if (verbose)
        fprintf(stderr, __NAME__": + Created barrier %lu (%d, %d) -> (%d, %d)\n",
                b, x1, y1, x2, y2);

    return b;
}

PointerBarrier *
create(Display *dpy, Window root, struct Insets *insets, int *num)
{
    XRRMonitorInfo *moninf;
    int i, barr_i, nmon;
    PointerBarrier *barriers = NULL;

    moninf = XRRGetMonitors(dpy, root, True, &nmon);
    if (nmon <= 0)
    {
        fprintf(stderr, __NAME__": No XRandR screens found\n");
        return NULL;
    }

    if (verbose)
        fprintf(stderr, __NAME__": We found %d XRandR screens\n", nmon);

    /* Per CRTC, we will create 4 barriers */
    *num = nmon * 4;

    barriers = calloc(*num, sizeof (PointerBarrier));
    if (barriers == NULL)
    {
        fprintf(stderr, __NAME__": Could not allocate memory for pointer "
                "barriers\n");
        return NULL;
    }

    barr_i = 0;
    for (i = 0; i < nmon; i++)
    {
        /* Top, left, right, bottom */
        barriers[barr_i++] = create_barrier_verbose(
                dpy, root,
                moninf[i].x, moninf[i].y + insets->top,
                moninf[i].x + moninf[i].width, moninf[i].y + insets->top,
                BarrierPositiveY, 0, NULL
        );
        barriers[barr_i++] = create_barrier_verbose(
                dpy, root,
                moninf[i].x + insets->left, moninf[i].y,
                moninf[i].x + insets->left, moninf[i].y + moninf[i].height,
                BarrierPositiveX, 0, NULL
        );
        barriers[barr_i++] = create_barrier_verbose(
                dpy, root,
                moninf[i].x + moninf[i].width - insets->right, moninf[i].y,
                moninf[i].x + moninf[i].width - insets->right, moninf[i].y + moninf[i].height,
                BarrierNegativeX, 0, NULL
        );
        barriers[barr_i++] = create_barrier_verbose(
                dpy, root,
                moninf[i].x, moninf[i].y + moninf[i].height - insets->bottom,
                moninf[i].x + moninf[i].width, moninf[i].y + moninf[i].height - insets->bottom,
                BarrierNegativeY, 0, NULL
        );
    }

    XRRFreeMonitors(moninf);
    XSync(dpy, False);
    return barriers;
}

void
destroy(Display *dpy, PointerBarrier *barriers, int num)
{
    int i;

    for (i = 0; i < num; i++)
    {
        XFixesDestroyPointerBarrier(dpy, barriers[i]);
        if (verbose)
            fprintf(stderr, __NAME__": - Destroyed barrier %lu\n", barriers[i]);
    }

    free(barriers);
    XSync(dpy, False);
}

void
handle_sigusr1(int dummy)
{
    (void)dummy;

    do_toggle = 1;
}

int
main(int argc, char **argv)
{
    Display *dpy;
    int fixes_opcode, fixes_event_base, fixes_error_base;
    int screen;
    Window root;
    XEvent ev;
    XConfigureEvent *cev;
    struct Insets insets;
    PointerBarrier *barriers = NULL;
    int barriers_num;
    struct sigaction sa;
    fd_set fds;
    int xfd;
    int barriers_active = 1;

    if (argc < 5)
    {
        fprintf(stderr, "Usage: "__NAME__" <top> <left> <right> <bottom> [-v]\n");
        exit(EXIT_FAILURE);
    }

    insets.top = atoi(argv[1]);
    insets.left = atoi(argv[2]);
    insets.right = atoi(argv[3]);
    insets.bottom = atoi(argv[4]);

    if (argc == 6 && strncmp(argv[5], "-v", 2) == 0)
        verbose = 1;

    dpy = XOpenDisplay(NULL);
    if (!dpy)
    {
        fprintf(stderr, __NAME__": Cannot open display\n");
        exit(EXIT_FAILURE);
    }

    if (!XQueryExtension(dpy, "XFIXES", &fixes_opcode, &fixes_event_base,
        &fixes_error_base))
    {
        fprintf(stderr, __NAME__": No XFIXES extension available\n");
        exit(EXIT_FAILURE);
    }

    /* Note: SA_RESTART is not set, which means that syscalls will
     * return with errno = EINTR when a signal is sent. This is crucial. */
    sa.sa_handler = handle_sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, NULL) == -1)
    {
        perror("Cannot set up handler for SIGUSR1");
        exit(EXIT_FAILURE);
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    /* The xlib docs say: On a POSIX system, the connection number is
     * the file descriptor associated with the connection. */
    xfd = ConnectionNumber(dpy);

    barriers = create(dpy, root, &insets, &barriers_num);

    /* Selecting for StructureNotifyMask will advise the X server to
     * send us ConfigureNotify events when the size of the root window
     * changes */
    XSelectInput(dpy, root, StructureNotifyMask);
    XSync(dpy, False);
    for (;;)
    {
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);

        if (select(xfd + 1, &fds, NULL, NULL, NULL) == -1 && errno != EINTR)
        {
            perror(__NAME__": select() returned with error");
            exit(EXIT_FAILURE);
        }

        while (XPending(dpy))
        {
            XNextEvent(dpy, &ev);

            if (ev.type == ConfigureNotify)
            {
                cev = &ev.xconfigure;
                if (verbose)
                    fprintf(stderr, __NAME__": Got ConfigureNotify, size %dx%d\n",
                            cev->width, cev->height);

                if (barriers)
                    destroy(dpy, barriers, barriers_num);

                if (barriers_active)
                    barriers = create(dpy, root, &insets, &barriers_num);
                else
                    barriers = NULL;
            }
        }

        if (do_toggle)
        {
            if (verbose)
                fprintf(stderr, __NAME__": Received signal, toggling\n");

            do_toggle = 0;
            barriers_active = !barriers_active;

            if (barriers)
                destroy(dpy, barriers, barriers_num);

            if (barriers_active)
                barriers = create(dpy, root, &insets, &barriers_num);
            else
                barriers = NULL;
        }
    }

    exit(EXIT_SUCCESS);
}
