/*
	Display 16-color RAW images in vga16fb framebuffer

	Wojciech Mu³a
	wojciech_mula@poczta.onet.pl
	license BSD

	compile:
		gcc fbi16.c -o fbi16.bin

Changelog:
	13.10.2006
		- fixed stupid error
	12.10.2006
		- display 16-color images (but needs root privilages)
	11.10.2006
		- more errors are detected
		- read 16-bit PGMs
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

#define CTRL_IDX	0x3ce
#define CTRL_DATA	0x3cf
#define SEQ_IDX		0x3c4
#define SEQ_DATA	0x3c5

typedef char bool;
#define false 0
#define true  1

#include <errno.h>
extern int errno;

uint8_t *screen;		/* framebuffer memory */
int      screen_size;	/* its size in bytes */

uint8_t *plane0;		/* b/w image (splitted into planes) */
uint8_t *plane1;		/* b/w image */
uint8_t *plane2;		/* b/w image */
uint8_t *plane3;		/* b/w image */

int width;				/* image width */
int blocks;				/* rounded up width/8 */
int height;				/* image height */
int dx, dy;				/* coordinates of left upper corner of displayed
                           image's portion */

int sdx, sdy;

uint8_t LUT[16][3];

/* initialzes program: opens files, registers signal handlers, etc. */
void init();

/* function that restore setting after our changes */
void clean();

/* show shifed image */
void show_image(int dx, int dy);

/* reads RGB file */
void read_raw(FILE *f);

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
	char* filename;
	int  i;
	char *e;
	
	int  pdx, pdy;

	/* Parse command line */
	if (argc < 4) {
		puts("Usage: fbi16 width height file.rgb");
		return 0;
	}
	else {
		filename = argv[3];

		width = strtol(argv[1], &e, 10);
		if (*e != 0 || width <= 0) {
			puts("Invalid width");
			return 1;
		}

		height = strtol(argv[2], &e, 10);
		if (*e != 0 || height <= 0) {
			puts("Invalid height");
			return 1;
		}
	}

	/* Initialize some things */
	init();

	/* Try to load image */ 
	f = fopen(filename, "rb"); halt_on_error(filename);
	read_raw(f);
	fclose(f);

	/* Set palette */
	for (i=0; i<16; i++)
		printf("\033]P%x%02x%02x%02x", i, LUT[i][0], LUT[i][1], LUT[i][2]);
	
	/* ESC [ 2 J -- erase whole screen */
	printf("\033[2J");
	fflush(stdout);
	
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

	/* Enter into interactive loop */
	pdx = pdy = dx = dy = 0;
	while (!quit) {
		if (refresh || pdx != dx || pdy != dy) {
			show_image(dx, dy);
			refresh = false;
			pdx     = dx;
			pdy     = dy;
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
				refresh = true;
				break;
		}
	}

	clean();
	return EXIT_SUCCESS;
}

/* implementation ***************************************************/

void halt_on_error(char* info) {
	int olderrno;
	if (errno != 0) {
		olderrno = errno;
		clean();
		fprintf(stdout, "%s [%d]: %s\n", info, olderrno, strerror(olderrno));
		exit(EXIT_FAILURE);
	}
}

void error(char* info) {
	clean();
	fprintf(stdout, "%s\n", info);
	exit(EXIT_FAILURE);
}

int total_colors = 0;
int8_t get_color(uint8_t r, uint8_t g, uint8_t b) {
	int i;
	/* naive linear searching, but we have max 16 colors */
	for (i=0; i<total_colors; i++)
		if (LUT[i][0] == r && LUT[i][1] == g && LUT[i][2] == b)
			return i;

	LUT[i][0] = r;
	LUT[i][1] = g;
	LUT[i][2] = b;
	return ++total_colors;
}

void read_raw(FILE *f) {
	uint8_t* line;
	uint8_t r,g,b, col, bit;
	int y, x;

	blocks	= (width+7)/8;
	line = (uint8_t*)malloc(3*8*blocks);
	if (line == NULL)
		error("malloc failed (1)");
	else
		memset(line, 0, 8*blocks);

	plane0 = (uint8_t*)malloc(blocks * height); if (plane0 == NULL) error("malloc failed (plane0)");
	plane1 = (uint8_t*)malloc(blocks * height); if (plane1 == NULL) error("malloc failed (plane1)");
	plane2 = (uint8_t*)malloc(blocks * height); if (plane2 == NULL) error("malloc failed (plane2)");
	plane3 = (uint8_t*)malloc(blocks * height); if (plane3 == NULL) error("malloc failed (plane3)");
	memset(plane0, 0, blocks * height);
	memset(plane1, 0, blocks * height);
	memset(plane2, 0, blocks * height);
	memset(plane3, 0, blocks * height);
	
	/* read file line by line */

	for (y=0; y < height; y++) {
		if (fread((void*)line, width, 3, f) < 3) 
			error("Truncated file (are width & height correct?)");
		halt_on_error("fread");
		
		for (x=0; x < width; x++) {
			/* read R, G, B components */
			r = line[x*3 + 0];
			g = line[x*3 + 1];
			b = line[x*3 + 2];

			/* allocate color index */
			col = get_color(r,g,b);
			if (total_colors > 16)
				error("This program display images contains at most 16 colors.");

			/* and split index into separate planes */
			bit = 7-(x & 0x7);
			plane0[y*blocks + x/8] |= ((col & 0x01) >> 0) << bit;
			plane1[y*blocks + x/8] |= ((col & 0x02) >> 1) << bit;
			plane2[y*blocks + x/8] |= ((col & 0x04) >> 2) << bit;
			plane3[y*blocks + x/8] |= ((col & 0x08) >> 3) << bit;
		}
	}
	free(line);
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

	/* obtain permissions to modify */ 
	/* 1. graphics controler */
	ioperm(CTRL_IDX,  1, 1);	ordie("ioperm (1)");
	ioperm(CTRL_DATA, 1, 1);	ordie("ioperm (2)");
	/* 2. sequencer */
	ioperm(SEQ_IDX,   1, 1);	ordie("ioperm (3)");
	ioperm(SEQ_DATA,  1, 1);	ordie("ioperm (4)");
	
	/* set signal handlers */
	signal(SIGINT,  sig_break); ordie("SIGINT");
	signal(SIGTERM, sig_break); ordie("SIGTERM");
	signal(SIGABRT, sig_break); ordie("SIGABRT");

	
	/* open framebuffer */
	fb_fd	= open("/dev/fb0", O_RDWR | O_NONBLOCK); ordie("/dev/fb0");
	
	/* get some info about framebuffer */
	ioctl(fb_fd, FBIOGET_VSCREENINFO, &varscreeninfo); ordie("varscreeninfo");
	if (varscreeninfo.type != FB_TYPE_VGA_PLANES)
		error("This program supports just vga16fb framebuffer");
	ioctl(fb_fd, FBIOGET_FSCREENINFO, &fixscreeninfo); ordie("fixscreeninfo");

	/* map video memory to our memory segment */
	screen_size = fixscreeninfo.smem_len;
	screen = mmap((void*)fixscreeninfo.smem_start, screen_size,
                  PROT_READ|PROT_WRITE, MAP_SHARED, fb_fd, 0);


	/* take over virtual terminal switching */
	ioctl(tty_fd, VT_GETMODE, &s); ordie("VT_GETMODE");
	sa.sa_handler = vt_activate;
	sigaction(SIGUSR1, &sa, NULL); ordie("sigaction(SIGUSR1)");
	sa.sa_handler = vt_release;
	sigaction(SIGUSR2, &sa, NULL); ordie("sigaction(SIGUSR2)");

	s.mode   = VT_PROCESS;
	s.acqsig = SIGUSR1;		/* SIGUSER1 is sent on switch to our vt con */
	s.relsig = SIGUSR2;		/* SIGUSER2 is sent on switch to another vt con */

	ioctl(tty_fd, VT_SETMODE, &s); ordie("VT_SETMODE");

	/* ESC 7 -- terminal: save current state */
	printf("\0337");
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
	/* ESC ] R -- reset palette */
	printf("\033]R");

}

void EGA_mask_planes(int n) {
	/* Enable set/reset (index 1) */
	outb(1, CTRL_IDX);
	outb(0x0f, CTRL_DATA);

	outb(2, SEQ_IDX);
	outb(n, SEQ_DATA);
}

void EGA_set_write_mode(uint8_t mode) {
	volatile uint8_t p;

	outb(5, CTRL_IDX);
	p = inb(CTRL_DATA);
	p = (p & ~0xfc) | (mode & 0x3);	/* modify only 2 lower bits */
	outb(5, CTRL_IDX);
	outb(p, CTRL_DATA);
}

void EGA_set_color(uint8_t color) {
	/* Set/rest (index 0) -- pixel color */
	outb(0, CTRL_IDX);
	outb(color & 0xf, CTRL_DATA);
}

void show_image(int dx, int dy) {
	int y, w, h;
	int screen_offset, plane_offset;
	
	volatile uint8_t p;

#ifdef _SETMODE
	ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
#endif
	EGA_set_write_mode(3);
	EGA_set_color(0xff);
	
	/* Data rotate & function select (index 3) */
	outb(3, CTRL_IDX);
	outb(0x00, CTRL_DATA);	/* no rotate, color no change */
	
	/* Bit mask (index 8) */
	outb(8, CTRL_IDX);
	outb(0xff, CTRL_DATA);	/* enable all bits */

	w = width  > 640 ? 640/8 : blocks;
	h = height > 480 ? 480   : height;

	screen_offset = sdy * 640/8 + sdx;
	plane_offset  = dy  * blocks + dx;
	for (y=0; y<h; y++) {

		/* clear current line */
		EGA_set_color(0x00);
		EGA_mask_planes(0x0f); /* enable all planes */
		memset(&screen[screen_offset], 0xff, 640/8);

		
		EGA_set_color(0xff);

		p = screen[screen_offset];
		/* copy plane 0 */
		EGA_mask_planes(0x0f);
		memcpy(&screen[screen_offset], &plane0[plane_offset], w);
		
		/* copy plane 1 */
		EGA_mask_planes(0x02);
		memcpy(&screen[screen_offset], &plane1[plane_offset], w);
		
		/* copy plane 2 */
		EGA_mask_planes(0x04);
		memcpy(&screen[screen_offset], &plane2[plane_offset], w);
		
		/* copy plane 3 */
		EGA_mask_planes(0x08);
		memcpy(&screen[screen_offset], &plane3[plane_offset], w);
		

		/* next line */
		screen_offset += 640/8;
		plane_offset  += blocks;
	}

	EGA_mask_planes(0x0f);
	

#ifdef _SETMODE
	ioctl(tty_fd, KDSETMODE, KD_TEXT);
#endif
}


void vt_activate(int dummy) {
	ioctl(tty_fd, VT_RELDISP, VT_ACKACQ);
}

void vt_release(int dummy) {
	ioctl(tty_fd, VT_RELDISP, 0); /* do not allow console switching */
}

void sig_break(int _) {
	clean();
	exit(EXIT_SUCCESS);
}

/*
vim: ts=4 sw=4 nowrap noexpandtab
*/
