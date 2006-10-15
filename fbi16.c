/*
	Display b/w PGM images in vga16fb framebuffer

	Wojciech Mu³a
	wojciech_mula@poczta.onet.pl
	license BSD

	compile:
		gcc fbi16.c -o fbi16.bin

Changelog:
	15.10.2006
		- center images
		- do not panic if open in virtual terminal
	13.10.2006
		- small changes (cleanup code, faster show_image)
	11.10.2006
		- more errors are detected
		- read 16-bit PGMs
		- invert image
	10.10.2006
		- switch terminal to raw mode
		- take control over virtual terminal switching
	9.10.2006
		- initial work
*/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/io.h>

#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>


#define _SETMODE

typedef char bool;
#define false 0
#define true  1

#include <errno.h>
extern int errno;

uint8_t *screen;		/* framebuffer memory */
int      screen_size;	/* in bytes */

uint8_t *image;			/* b/w image */
int width;				/* image width */
int blocks;				/* rounded up width/8 */
int height;				/* image height */
int dx, dy;				/* coordinates of left upper corner of displayed
                           image's portion */
int sdx, sdy;

/* initialzes program: opens files, registers signal handlers, etc. */
void init();

/* function that restore setting after our changes */
void clean();

/* show shifed image */
void show_image(int dx, int dy);

/* reads PGM file and binarizes it */
void read_pgm(FILE *f);

/* as name states */
void invert_image();


void halt_on_error(char*);
#define ordie halt_on_error

/* handlers called on activate & release virtual terminal */
void vt_release(int dummy);
void vt_activate (int dummy);

/* common handler for several signals (SIGTERM, SIGABRT, etc.) */
void sig_break(int _);

int main(int argc, char* argv[]) {
	bool quit		= false;
	bool refresh	= true;
	FILE *f;
	char *filename;
	int  pdx, pdy;

	if (argc < 2) {
		puts("Usage: fbi16 [file]");
		exit(0);
	}
	else
		filename = argv[1];
	
	init();

	image = NULL;
	f = fopen(filename, "rb"); halt_on_error(filename);
	read_pgm(f);
	fclose(f);


	/* center horizontal */
	if (blocks < 640/8)
		sdx = (640/8 - blocks)/2;
	else
		sdx = 0;
	
	/* center verical */
	if (height < 480)
		sdy = (480 - height)/2;
	else
		sdy = 0;

	pdx = pdy = dx = dy = 0;
	while (!quit) {
		if (refresh || pdx != dx || pdy != dy) {
			show_image(dx, dy);
			pdx = dx;
			pdy = dy;
			refresh = false;
		}
		switch (getchar()) {
			case 'q':
			case 'Q':
				quit = true;
				break;

			/* scroll left */
			case 's':
				if (blocks > 640/8) {
					dx += 1;
					if (dx > (blocks - 640/8))
						dx = blocks - 640/8;
				}
				break;
			case 'S':
				if (blocks > 640/8) {
					dx += 2;
					if (dx > (blocks - 640/8))
						dx = blocks - 640/8;
				}
				break;

			/* scroll right */
			case 'a':
				if (blocks > 640/8) {
					dx -= 1;
					if (dx < 0) dx = 0;
					refresh = true;
				}
				break;
			case 'A':
				if (blocks > 640/8) {
					dx -= 2;
					if (dx < 0) dx = 0;
					refresh = true;
				}
				break;

			/* scroll down */
			case 'w':
				if (height > 480) {
					dy += 10;
					if (dy > (height - 480))
						dy = height - 480;
					refresh = true;
				}
				break;
			case 'W':
				if (height > 480) {
					dy += 20;
					if (dy > (height - 480))
						dy = height - 480;
					refresh = true;
				}
				break;
			
			/* scroll up */
			case 'z':
				if (height > 480) {
					dy -= 10;
					if (dy < 0) dy = 0;
					refresh = true;
				}
				break;
			case 'Z':
				if (height > 480) {
					dy -= 20;
					if (dy < 0) dy = 0;
					refresh = true;
				}
				break;

			/* refresh image */
			case '\n':
			case 'r':
			case 'R':
				refresh = true;
				break;

			/* invert image */ 
			case 'i':
			case 'I':
				invert_image();
				refresh = true;
				break;
		}
	}

	clean();
	return EXIT_SUCCESS;
}

/* implemetation ****************************************************/

void halt_on_error(char* info) {
	if (errno != 0) {
		clean();
		fprintf(stdout, "%s: %s\n", info, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void error(char* info) {
	clean();
	fprintf(stdout, "%s\n", info);
	exit(EXIT_FAILURE);
}

void read_pgm(FILE *f) {
	int maxval;

	uint8_t* line;
	uint8_t* imgpix;
	int y, x;
	int c;

	c       = fscanf(f, "P5\n%d %d\n%d", &width, &height, &maxval);
	if (c < 3)
		error("Not a PGM file");
	/*if (maxval > 255)
		error("16-bit PGM not supported");*/
	
	blocks	= (width+7)/8;
	if (maxval > 255) {
		line = (uint8_t*)malloc(8*blocks*2);
		if (line == NULL)
			error("malloc failed (1)");
		else
			memset(line, 0, 8*blocks*2);
	}
	else {
		line = (uint8_t*)malloc(8*blocks);
		if (line == NULL)
			error("malloc failed (2)");
		else
			memset(line, 0, 8*blocks);
	}

	image	= (uint8_t*)malloc(blocks * height);
	if (image == NULL) error("malloc failed");
	
	imgpix	= &image[0];

	/* read file line by line */

	if (maxval < 256) {
		for (y=0; y < height; y++) {
			if (fread((void*)line, width, 1, f) < 1) 
				error("Truncated PGM file");
			halt_on_error("fread");
			
			for (x=0; x < blocks; x++)
				*imgpix++ = (
					(line[x*8 + 0] > 127 ? 0x80 : 0x00) |
					(line[x*8 + 1] > 127 ? 0x40 : 0x00) |
					(line[x*8 + 2] > 127 ? 0x20 : 0x00) |
					(line[x*8 + 3] > 127 ? 0x10 : 0x00) |
					(line[x*8 + 4] > 127 ? 0x08 : 0x00) |
					(line[x*8 + 5] > 127 ? 0x04 : 0x00) |
					(line[x*8 + 6] > 127 ? 0x02 : 0x00) |
					(line[x*8 + 7] > 127 ? 0x01 : 0x00)
				);
		}
	}
	else { /* maxval >= 256 -> 2 bytes per pixel */
		for (y=0; y < height; y++) {
			if (fread((void*)line, 2*width, 1, f) < 1) 
				error("Truncated PGM file");
			halt_on_error("fread");
			
			for (x=0; x < blocks; x++)
				*imgpix++ = (
					(line[x*16 +  1] > 127 ? 0x80 : 0x00) |
					(line[x*16 +  3] > 127 ? 0x40 : 0x00) |
					(line[x*16 +  5] > 127 ? 0x20 : 0x00) |
					(line[x*16 +  7] > 127 ? 0x10 : 0x00) |
					(line[x*16 +  9] > 127 ? 0x08 : 0x00) |
					(line[x*16 + 11] > 127 ? 0x04 : 0x00) |
					(line[x*16 + 13] > 127 ? 0x02 : 0x00) |
					(line[x*16 + 15] > 127 ? 0x01 : 0x00)
				);
		}
	}
	free(line);
}

void invert_image() {
	uint8_t *pix;
	int     i, n;

	pix = &image[0];
	n   = blocks*height;
	for (i=0; i<n; i++) {
		*pix = ~*pix;
		 pix++;
	}
}

int fb_fd, tty_fd;
struct termios term;

void init() {
	int old_clflag;
	struct fb_fix_screeninfo fixscreeninfo;
	struct fb_fix_screeninfo varscreeninfo;
	
	struct sigaction sa;
	struct vt_mode s;

	
	/* open terminal */
	tty_fd	= open("/dev/tty", O_RDWR); halt_on_error("/dev/tty");
	
	/* set raw mode (and save terminal settings) */
	tcgetattr(tty_fd, &term); halt_on_error("tcgetattr");
	old_clflag		 = term.c_lflag;
	term.c_lflag	&= ~(ICANON | ECHO);
	tcsetattr(tty_fd, TCSAFLUSH, &term); halt_on_error("tcsetattr");
	term.c_lflag	 = old_clflag;
	
	/* set signal handlers */
	signal(SIGINT,  sig_break); ordie("SIGINT");
	signal(SIGTERM, sig_break); ordie("SIGTERM");
	signal(SIGABRT, sig_break); ordie("SIGABRT");

	
	/* open framebuffer */
	fb_fd	= open("/dev/fb0", O_RDWR | O_NONBLOCK); ordie("/dev/fb0");
	
	/* get some info about framebuffer */
	ioctl(fb_fd, FBIOGET_VSCREENINFO, &varscreeninfo); ordie("varscreen");
	if (varscreeninfo.type != FB_TYPE_VGA_PLANES)
		error("This program supports just vga16fb framebuffer");
	ioctl(fb_fd, FBIOGET_FSCREENINFO, &fixscreeninfo); ordie("fixscreen");

	/* map video memory to our memory segment */
	screen_size = fixscreeninfo.smem_len;
	screen = mmap((void*)fixscreeninfo.smem_start, screen_size,
                  PROT_READ|PROT_WRITE, MAP_SHARED, fb_fd, 0);
	halt_on_error("mmap");

	/* take over virtual terminal switching (if we are running in VT) */
	if (ioctl(tty_fd, VT_GETMODE, &s) == 0) {
		sa.sa_handler = vt_activate;
		sigaction(SIGUSR1, &sa, NULL); ordie("sigaction(SIGUSR1)");
		sa.sa_handler = vt_release;
		sigaction(SIGUSR2, &sa, NULL); ordie("sigaction(SIGUSR2)");

		s.mode   = VT_PROCESS;
		s.acqsig = SIGUSR1;		/* SIGUSER1 is sent on switch to our con */
		s.relsig = SIGUSR2;		/* SIGUSER2 is sent on switch to another con */

		ioctl(tty_fd, VT_SETMODE, &s); ordie("VT_SETMODE");
	}
	else
		errno = 0; /* reset errno (set by ioctl) */

	/* ESC 7 -- terminal: save current state */
	printf("\0337");
	/* ESC [ 2 J -- terminal: erase whole screen */
	printf("\033[2J");
}

void clean() {
#if SETMODE
	/* set console mode (text, rather graphics) */
	ioctl(tty_fd, KDSETMODE, KD_TEXT);
#endif
	
	/* restore terminal mode */
	tcsetattr(tty_fd, TCSAFLUSH, &term);
	
	/* close opened files */
	close(fb_fd);
	close(tty_fd);

	/* and unmap video memory */
	munmap(screen, screen_size);

	/* ESC [ 2 J -- erase whole screen */
	printf("\033[2J");
	/* ESC 8 -- restore saved state */
	printf("\0338");
}

void show_image(int dx, int dy) {
	int w, h, y;
	int screen_offset, image_offset;

	printf("\033[1m"		/* set bright */
	       "\033[37m"		/*     white foreground */
	       "\033[40m"		/* and black background */
	       " \033[1;1H"		/* move cursor to left upper corner */
	); fflush(stdout);
#ifdef _SETMODE
	ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
#endif

	h = height > 480 ? 480   : height;
	w = width  > 640 ? 640/8 : blocks;

	screen_offset = sdy * 640/8 + sdx;
	image_offset  = dy  * blocks + dx;
	for (y=0; y<h; y++) {
		memcpy(&screen[screen_offset], &image[image_offset], w);

		screen_offset += 640/8;
		image_offset  += blocks;
	}
#ifdef _SETMODE
	ioctl(tty_fd, KDSETMODE, KD_TEXT);
#endif
}


void vt_activate(int dummy) {
	show_image(dx, dy);
	ioctl(tty_fd, VT_RELDISP, VT_ACKACQ);
}

void vt_release(int dummy) {
	ioctl(tty_fd, VT_RELDISP, 1);
}

void sig_break(int _) {
	clean();
	exit(EXIT_SUCCESS);
}

/*
vim: ts=4 sw=4 nowrap noexpandtab
*/
