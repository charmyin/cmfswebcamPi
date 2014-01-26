/* fswebcam - Small and simple webcam for *nix                */
/*============================================================*/
/* Copyright (C)2005-2011 Philip Heron <phil@sanslogic.co.uk> */
/*                                                            */
/* This program is distributed under the terms of the GNU     */
/* General Public License, version 2. You may use, modify,    */
/* and redistribute it under the terms of this license. A     */
/* copy should be included with this source.                  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <gd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "fswebcam.h"
#include "log.h"
#include "src.h"
#include "dec.h"
#include "effects.h"
#include "parse.h"

#define ALIGN_LEFT   (0)
#define ALIGN_CENTER (1)
#define ALIGN_RIGHT  (2)

#define NO_BANNER     (0)
#define TOP_BANNER    (1)
#define BOTTOM_BANNER (2)

#define FORMAT_JPEG (0)
#define FORMAT_PNG  (1)

enum fswc_options {
	OPT_VERSION = 128,
	OPT_PID,
	OPT_OFFSET,
	OPT_LIST_INPUTS,
	OPT_LIST_TUNERS,
	OPT_LIST_FORMATS,
	OPT_LIST_CONTROLS,
	OPT_LIST_FRAMESIZES,
	OPT_LIST_FRAMERATES,
	OPT_BRIGHTNESS,
	OPT_HUE,
	OPT_COLOUR,
	OPT_CONTRAST,
	OPT_WHITENESS,
	OPT_REVERT,
	OPT_FLIP,
	OPT_CROP,
	OPT_SCALE,
	OPT_ROTATE,
	OPT_DEINTERLACE,
	OPT_INVERT,
	OPT_GREYSCALE,
	OPT_SWAPCHANNELS,
	OPT_NO_BANNER,
	OPT_TOP_BANNER,
	OPT_BOTTOM_BANNER,
	OPT_BG_COLOUR,
	OPT_BL_COLOUR,
	OPT_FG_COLOUR,
	OPT_FONT,
	OPT_NO_SHADOW,
	OPT_SHADOW,
	OPT_TITLE,
	OPT_NO_TITLE,
	OPT_SUBTITLE,
	OPT_NO_SUBTITLE,
	OPT_TIMESTAMP,
	OPT_NO_TIMESTAMP,
	OPT_GMT,
	OPT_INFO,
	OPT_NO_INFO,
	OPT_UNDERLAY,
	OPT_NO_UNDERLAY,
	OPT_OVERLAY,
	OPT_NO_OVERLAY,
	OPT_JPEG,
	OPT_PNG,
	OPT_SAVE,
	OPT_EXEC,
	OPT_DUMPFRAME,
	OPT_FPS,
};

typedef struct {
	
	/* List of options. */
	char *opts;
	const struct option *long_opts;
	
	/* When reading from the command line. */
	int opt_index;
	
	/* When reading from a configuration file. */
	char *filename;
	FILE *f;
	size_t line;
	
} fswc_getopt_t;

typedef struct {
	uint16_t id;
	char    *options;
} fswebcam_job_t;

typedef struct {
	
	/* General options. */
	unsigned long loop;
	signed long offset;
	unsigned char background;
	char *pidfile;
	char *logfile;
	char gmt;
	
	/* Capture start time. */
	time_t start;
	
	/* Device options. */
	char *device;
	char *input;
	unsigned char tuner;
	unsigned long frequency;
	unsigned long delay;
	char use_read;
	uint8_t list;
	
	/* Image capture options. */
	unsigned int width;
	unsigned int height;
	unsigned int frames;
	unsigned int fps;
	unsigned int skipframes;
	int palette;
	src_option_t **option;
	char *dumpframe;
	
	/* Job queue. */
	//uint8_t jobs;
	//fswebcam_job_t **job;
	
	/* Banner options. */
	char banner;
	uint32_t bg_colour;
	uint32_t bl_colour;
	uint32_t fg_colour;
	char *title;
	char *subtitle;
	char *timestamp;
	char *info;
	char *font;
	int fontsize;
	char shadow;
	
	/* Overlay options. */
	char *underlay;
	char *overlay;
	
	/* Output options. */
	char *filename;
	char format;
	char compression;
	
} fswebcam_config_t;

volatile char received_sigusr1 = 0;
volatile char received_sighup  = 0;
volatile char received_sigterm = 0;

void fswc_signal_usr1_handler(int signum)
{
	/* Catches SIGUSR1 */
	INFO("Caught signal SIGUSR1.");
	received_sigusr1 = 1;
}

void fswc_signal_hup_handler(int signum)
{
	/* Catches SIGHUP */
	INFO("Caught signal SIGHUP.");
	received_sighup = 1;
}

void fswc_signal_term_handler(int signum)
{
	char *signame;
	
	/* Catches SIGTERM and SIGINT */
	switch(signum)
	{
	case SIGTERM: signame = "SIGTERM"; break;
	case SIGINT:  signame = "SIGINT"; break;
	default:      signame = "Unknown"; break;
	}
	
	INFO("Caught signal %s", signame);
	received_sigterm = 1;
}

int fswc_setup_signals()
{
	signal(SIGUSR1, fswc_signal_usr1_handler);
	signal(SIGHUP,  fswc_signal_hup_handler);
	signal(SIGTERM, fswc_signal_term_handler);
	signal(SIGINT,  fswc_signal_term_handler);
	
	return(0);
}

char *fswc_strftime(char *dst, size_t max, char *src,
                    time_t timestamp, int gmt)
{
	struct tm tm_timestamp;
	
	/* Clear target string, and verify source is set */
	*dst = '\0';
	if(!src) return(dst);
	
	/* Set the time structure. */
	if(gmt) gmtime_r(&timestamp, &tm_timestamp);
	else localtime_r(&timestamp, &tm_timestamp);
	
	/* Create the string */
	strftime(dst, max, src, &tm_timestamp);
	
	return(dst);
}

char *fswc_strduptime(char *src, time_t timestamp, int gmt)
{
	struct tm tm_timestamp;
	char *dst;
	size_t l;
	
	if(!src) return(NULL);
	
	/* Set the time structure. */
	if(gmt) gmtime_r(&timestamp, &tm_timestamp);
	else localtime_r(&timestamp, &tm_timestamp);
	
	dst = NULL;
	l = strlen(src) * 2;
	
	while(1)
	{
		size_t r;
		char *t = realloc(dst, l);
		
		if(!t)
		{
			free(dst);
			return(NULL);
		}
		
		dst = t;
		
		*dst = 1;
		r = strftime(dst, l, src, &tm_timestamp);
		
		if(r > 0 && r < l) return(dst);
		if(r == 0 && *dst == '\0') return(dst);
		
		l *= 2;
	}
	
	return(NULL);
}

void fswc_DrawText(gdImagePtr im, char *font, double size,
                   int x, int y, char align,
                   uint32_t colour, char shadow, char *text)
{
	char *err;
	int brect[8];
	
	if(!text) return;
	
	if(shadow)
	{
		uint32_t scolour = colour & 0xFF000000;
		
		fswc_DrawText(im, font, size, x + 1, y + 1,
		              align, scolour, 0, text);
	}
	
	/* Correct alpha value for GD. */
	colour = (((colour & 0xFF000000) / 2) & 0xFF000000) +
	         (colour & 0xFFFFFF);
	
	/* Pre-render the text. We use the results during alignment. */
	err = gdImageStringFT(NULL, &brect[0], colour, font, size, 0.0, 0, 0, text);
	if(err)
	{
		WARN("%s", err);
		return;
	}
	
	/* Adjust the coordinates according to the alignment. */
	switch(align)
	{
	case ALIGN_CENTER: x -= brect[4] / 2; break;
	case ALIGN_RIGHT:  x -= brect[4];     break;
	}
	
	/* Render the text onto the image. */
	gdImageStringFT(im, NULL, colour, font, size, 0.0, x, y, text);
}

int fswc_draw_overlay(fswebcam_config_t *config, char *filename, gdImage *image){
	FILE *f;
	gdImage *overlay;
	
	if(!filename) return(-1);
	
	f = fopen(filename, "rb");
	if(!f)
	{
		ERROR("Unable to open '%s'", filename);
		ERROR("fopen: %s", strerror(errno));
		return(-1);
	}
	
	overlay = gdImageCreateFromPng(f);
	fclose(f);
	
	if(!overlay)
	{
		ERROR("Unable to read '%s'. Not a PNG image?", filename);
		return(-1);
	}
	
	gdImageCopy(image, overlay, 0, 0, 0, 0, overlay->sx, overlay->sy);
	gdImageDestroy(overlay);
	
	return(0);
}

int fswc_draw_banner(fswebcam_config_t *config, gdImage *image)
{
	char timestamp[200];
	int w, h;
	int height;
	int spacing;
	int top;
	int y;
	
	w = gdImageSX(image);
	h = gdImageSY(image);
	
	/* Create the timestamp text. */
	fswc_strftime(timestamp, 200, config->timestamp,
	              config->start, config->gmt);
	
	/* Calculate the position and height of the banner. */
	spacing = 4;
	height = config->fontsize + (spacing * 2);
	
	if(config->subtitle || config->info)
		height += config->fontsize * 0.8 + spacing;
	
	top = 0;
	if(config->banner == BOTTOM_BANNER) top = h - height;
	
	/* Draw the banner line. */
	if(config->banner == TOP_BANNER)
	{
		gdImageFilledRectangle(image,
		                       0, height + 1,
		                       w, height + 2,
		                       config->bl_colour);
	}
	else
	{
		gdImageFilledRectangle(image,
		                       0, top - 2,
		                       w, top - 1,
		                       config->bl_colour);
	}
	
	/* Draw the background box. */
	gdImageFilledRectangle(image,
	   0, top,
	   w, top + height,
	   config->bg_colour);
	
	y = top + spacing + config->fontsize;
	
	/* Draw the title. */
	fswc_DrawText(image, config->font, config->fontsize,
	              spacing, y, ALIGN_LEFT,
	              config->fg_colour, config->shadow, config->title);
	
	/* Draw the timestamp. */
	fswc_DrawText(image, config->font, config->fontsize * 0.8,
	              w - spacing, y, ALIGN_RIGHT,
	              config->fg_colour, config->shadow, timestamp);
	
	y += spacing + config->fontsize * 0.8;
	
	/* Draw the sub-title. */
	fswc_DrawText(image, config->font, config->fontsize * 0.8,
	              spacing, y, ALIGN_LEFT,
	              config->fg_colour, config->shadow, config->subtitle);
	
	/* Draw the info text. */
	fswc_DrawText(image, config->font, config->fontsize * 0.7,
	              w - spacing, y, ALIGN_RIGHT,
	              config->fg_colour, config->shadow, config->info);
	return(0);
}

gdImage* fswc_gdImageDuplicate(gdImage* src)
{
	gdImage *dst;
	
	dst = gdImageCreateTrueColor(gdImageSX(src), gdImageSY(src));
	if(!dst) return(NULL);
	
	gdImageCopy(dst, src, 0, 0, 0, 0, gdImageSX(src), gdImageSY(src));
	
	return(dst);
}

int fswc_output(fswebcam_config_t *config, char *name, gdImage *image)
{
	char filename[FILENAME_MAX];
	gdImage *im;
	FILE *f;
	
	if(!name) return(-1);
	if(!strncmp(name, "-", 2) && config->background)
	{
		ERROR("stdout is unavailable in background mode.");
		return(-1);
	}
	
	fswc_strftime(filename, FILENAME_MAX, name,
	              config->start, config->gmt);
	
	/* Create a temporary image buffer. */
	im = fswc_gdImageDuplicate(image);
	if(!im)
	{
		ERROR("Out of memory.");
		return(-1);
	}
	
	/* Draw the underlay. */
	fswc_draw_overlay(config, config->underlay, im);
	
	/* Draw the banner. */
	if(config->banner != NO_BANNER)
	{
		char *err;
		
		/* Check if drawing text works */
		err = gdImageStringFT(NULL, NULL, 0, config->font, config->fontsize, 0.0, 0, 0, "");
		
		if(!err) fswc_draw_banner(config, im);
		else
		{
			/* Can't load the font - display a warning */
			WARN("Unable to load font '%s': %s", config->font, err);
			WARN("Disabling the the banner.");
		}
	}
	
	/* Draw the overlay. */
	fswc_draw_overlay(config, config->overlay, im);
	
	/* Write to a file if a filename was given, otherwise stdout. */
	if(strncmp(name, "-", 2)) f = fopen(filename, "wb");
	else f = stdout;
	
	if(!f)
	{
		ERROR("Error opening file for output: %s", filename);
		ERROR("fopen: %s", strerror(errno));
		gdImageDestroy(im);
		return(-1);
	}
	
	/* Write the compressed image. */
	MSG("Writing JPEG image to '%s'.", filename);
	gdImageJpeg(im, f, config->compression);
	break;

	if(f != stdout) fclose(f);
	
	gdImageDestroy(im);
	
	return(0);
}

int fswc_exec(fswebcam_config_t *config, char *cmd)
{
	char *cmdline;
	FILE *p;
	
	cmdline = fswc_strduptime(cmd, config->start, config->gmt);
	if(!cmdline) return(-1);
	
	MSG("Executing '%s'...", cmdline);
	
	p = popen(cmdline, "r");
	free(cmdline);
	
	if(p)
	{
		while(!feof(p))
		{
			char *n;
			char line[1024];
			
			if(!fgets(line, 1024, p)) break;
			
			while((n = strchr(line, '\n'))) *n = '\0';
			MSG("%s", line);
		}
		
		pclose(p);
	}
	else
	{
		ERROR("popen: %s", strerror(errno));
		return(-1);
	}
	
	return(0);
}

int fswc_grab(fswebcam_config_t *config)
{
	int countImages=0;
	while(1){
			char* deviceName="/dev/video0";//strdup("/dev/video0")
			uint32_t frame;
			uint32_t x, y;
			avgbmp_t *abitmap, *pbitmap;
			gdImage *image, *original;
			uint8_t modified;
			src_t src;

			/* Set source options... */
			memset(&src, 0, sizeof(src));
			src.input      = config->input;
			src.tuner      = config->tuner;
			src.frequency  = config->frequency;
			src.delay      = config->delay;
			src.timeout    = 15; /* seconds */
			src.use_read   = config->use_read;
			src.list       = config->list;
			src.palette    = config->palette;
			src.width      = config->width;
			src.height     = config->height;
			src.fps        = config->fps;
			src.option     = config->option;

			HEAD("--- Opening %s...", deviceName);

			if(src_open(&src, deviceName) == -1) return(-1);

			if(src_open(&src, config->device) == -1) return(-1);

			/* The source may have adjusted the width and height we passed
			 * to it. Update the main config to match. */
			config->width  = src.width;
			config->height = src.height;
		
			/* Set the default values for this run. */
			if(config->font) free(config->font);
			if(config->title) free(config->title);
			if(config->subtitle) free(config->subtitle);
			if(config->timestamp) free(config->timestamp);
			if(config->info) free(config->info);
			if(config->underlay) free(config->underlay);
			if(config->overlay) free(config->overlay);
			if(config->filename) free(config->filename);

			config->banner       = BOTTOM_BANNER;
			config->bg_colour    = 0x40263A93;
			config->bl_colour    = 0x00FF0000;
			config->fg_colour    = 0x00FFFFFF;
			config->font         = strdup("sans");
			config->fontsize     = 10;
			config->shadow       = 1;
			config->title        = strdup("dapeng");
			config->subtitle     = strdup("001");
			config->timestamp    = strdup("%Y-%m-%d %H:%M:%S (%Z)");
			config->info         = strdup("yes it's ok");;
			config->underlay     = NULL;
			config->overlay      = NULL;
			config->filename     = NULL;
			config->format       = FORMAT_JPEG;
			config->compression  = -1;

			//
			char countImageStr[20];
			while(1){
				countImages++;
				/* Record the start time. */
				config->start = time(NULL);
				/* Allocate memory for the average bitmap buffer. */
				/* Allocate memory for the average bitmap buffer. */
				abitmap = calloc(config->width * config->height * 3, sizeof(avgbmp_t));
				if(!abitmap)
				{
					ERROR("Out of memory.");
					return(-1);
				}

				HEAD("--- Capturing frame...");

				//if required timeout , break
				if(src_grab(&src) != -1) {
					/* Add frame to the average bitmap. */
					fswc_add_image_jpeg(&src, abitmap);
				}else{
					break;
				}

				HEAD("--- Processing captured image...");

				/* Copy the average bitmap image to a gdImage. */
				original = gdImageCreateTrueColor(config->width, config->height);
				if(!original)
				{
					ERROR("Out of memory.");
					free(abitmap);
					return(-1);
				}

				pbitmap = abitmap;
				for(y = 0; y < config->height; y++)
					for(x = 0; x <  config->width; x++)
					{
						int px = x;
						int py = y;
						int colour;

						colour  = (*(pbitmap++) / config->frames) << 16;
						colour += (*(pbitmap++) / config->frames) << 8;
						colour += (*(pbitmap++) / config->frames);

						gdImageSetPixel(original, px, py, colour);
					}

				free(abitmap);

				/* Make a copy of the original image. */
				image = fswc_gdImageDuplicate(original);
				if(!image)
				{
					ERROR("Out of memory.");
					gdImageDestroy(image);
					return(-1);
				}

				/* Run through the jobs list. */

				MSG("Setting output format to JPEG, quality %i", 90);
				config->format = FORMAT_JPEG;
				config->compression = 90;

				snprintf(countImageStr, 20, "%d", countImages);
				//printf("countImageStr = %s\n", countImageStr);
				//Generate the imgName
				char imgName[30];
				char imgScaleName[30];
				bzero(imgName, 30);
				bzero(imgScaleName, 30);
				strcat(imgName, "img");
				strcat(imgName, countImageStr);
				strcat(imgName, ".jpg");
				strcat(imgScaleName, "scaleImg");
				strcat(imgScaleName, countImageStr);
				strcat(imgScaleName, ".jpg");
				//rotate the image
				//image = fx_rotate(image, "180");
				sleep(3);
				//save file, and scale
				//printf("++++++++++++config->job[x]->options %s\n", config->job[x]->options );
				fswc_output(config, imgName, image);
				//image = fx_scale(image, "256x192");
				//fswc_output(config, imgScaleName, image);
				gdImageDestroy(image);
				gdImageDestroy(original);

			}

			/* We are now finished with the capture card. */
			src_close(&src);
	}
	return(0);
}

int fswc_openlog(fswebcam_config_t *config)
{
	char *s;
	int r;
	
	/* Get the first part of the filename. */
	s = argdup(config->logfile, ":", 0, 0);
	if(!s)
	{
		ERROR("Out of memory.");
		return(-1);
	}
	
	if(!strcasecmp(s, "file"))
	{
		free(s);
		
		s = argdup(config->logfile, ":", 1, 0);
		if(!s)
		{
			ERROR("No log file was specified.");
			return(-1);
		}
	}
	else if(!strcasecmp(s, "syslog"))
	{
		free(s);
		log_syslog(1);
		return(0);
	}
	
	r = log_open(s);
	free(s);
	
	return(r);
}

int fswc_background(fswebcam_config_t *config)
{
	pid_t pid, sid;
	
	/* Silence the output if not logging to a file. */
	if(!config->logfile) log_set_fd(-1);
	
	/* Fork into the background. */
	pid = fork();
	
	if(pid < 0)
	{
		ERROR("Error going into the background.");
		ERROR("fork: %s", strerror(errno));
		return(-1);
	}
	
	/* Is this the parent process? If so, end it. */
	if(pid > 0) exit(0);
	
	umask(0);
	
	/* Create a new SID for the child process. */
	sid = setsid();
	if(sid < 0)
	{
		ERROR("Error going into the background.");
		ERROR("setsid: %s", strerror(errno));
		return(-1);
	}
	
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	
	return(0);
}

int fswc_savepid(fswebcam_config_t *config)
{
	FILE *fpid = fopen(config->pidfile, "wt");
	
	if(!fpid)
	{
		ERROR("Error saving PID to file '%s'",config->pidfile);
		ERROR("fopen: %s", strerror(errno));
		
		return(-1);
	}
	
	fprintf(fpid, "%i\n", getpid());
	fclose(fpid);
	
	return(0);
}

int fswc_find_palette(char *name)
{
	int i;
	
	/* Scan through the palette table until a match is found. */
	for(i = 0; src_palette[i].name != NULL; i++)
	{
		/* If a match was found, return the index. */
		if(!strcasecmp(src_palette[i].name, name)) return(i);
	}
	
	/* No match was found. */
	ERROR("Unrecognised palette format \"%s\". Supported formats:", name);
	
	for(i = 0; src_palette[i].name != NULL; i++)
		ERROR("%s", src_palette[i].name);
	
	return(-1);
}


int fswc_set_option(fswebcam_config_t *config, char *option)
{
	char *name, *value;
	
	if(!option) return(-1);
	
	name = strdup(option);
	if(!name)
	{
		ERROR("Out of memory.");
		return(-1);
	}
	
	value = strchr(name, '=');
	if(value)
	{
		*(value++) = '\0';
		if(*value == '\0') value = NULL;
	}
	
	src_set_option(&config->option, name, value);
	
	free(name);
	
	return(0);
}

int fswc_getopt_file(fswc_getopt_t *s)
{
	char line[1024];
	char *arg, *val;
	struct option *opt;
	
	while(fgets(line, 1024, s->f))
	{
		s->line++;
		strtrim(line, WHITESPACE);
		arg = argdup(line, WHITESPACE, 0, 0);
		
		if(!arg) continue;
		if(*arg == '#') continue;
		
		/* Find argument in the list. */
		opt = (struct option *) s->long_opts;
		while(opt->name)
		{
			if(!strcasecmp(opt->name, arg)) break;
			opt++;
		}
		
		if(!opt->name)
		{
			ERROR("Unknown argument: %s", arg);
			WARN("%s,%i: %s", s->filename, s->line, line);
			free(arg);
			return(-2);
		}
		
		if(opt->val == 'c')
		{
			ERROR("You can't use config from a configuration file.");
			WARN("%s,%i: %s", s->filename, s->line, line);
			free(arg);
			return(-2);
		}
		
		if(opt->has_arg)
		{
			val = argdup(line, WHITESPACE, 1, 0);
			optarg = val;
		}
		
		free(arg);
		
		return(opt->val);
	}
	
	/* Have we reached the end of the file? */
	if(feof(s->f)) return(-1);
	
	/* No.. there has been an error. */
	ERROR("fread: %s", strerror(errno));
	
	return(-2);
}



int fswc_getopts(fswebcam_config_t *config, int argc, char *argv[])
{
	
	/* Set the defaults. */
	config->loop = 0;
	config->offset = 0;
	config->background = 0;
	config->pidfile = NULL;
	config->logfile = NULL;
	config->gmt = 0;
	config->start = 0;
	config->device = strdup("/dev/video0");
	config->input = NULL;
	config->tuner = 0;
	config->frequency = 0;
	config->delay = 0;
	config->use_read = 0;
	config->list = 0;
	config->width = 1024;
	config->height = 768;
	config->fps = 0;
	config->frames = 1;
	config->skipframes = 0;
	config->palette = SRC_PAL_ANY;
	config->option = NULL;
	config->dumpframe = NULL;
	//config->jobs = 0;
	//config->job = NULL;
	
	/* Don't report errors. */
	opterr = 0;
	
	/* Reset getopt to ensure parsing begins at the first argument. */
	optind = 0;
	
	return(0);
}

int fswc_free_config(fswebcam_config_t *config)
{
	free(config->pidfile);
	free(config->logfile);
	free(config->device);
	free(config->input);
	
	free(config->dumpframe);
        free(config->title);
	free(config->subtitle);
	free(config->timestamp);
	free(config->info);
	free(config->font);
	free(config->underlay);
	free(config->overlay);
	free(config->filename);
	
	src_free_options(&config->option);
	//fswc_free_jobs(config);
	
	memset(config, 0, sizeof(fswebcam_config_t));
	
	return(0);
}

//this integer for image name
int main(int argc, char *argv[])
{
	
	fswebcam_config_t *config;

	/* Prepare the configuration structure. */
	config = calloc(sizeof(fswebcam_config_t), 1);
	if(!config)
	{
		WARN("Out of memory.");
		return(-1);
	}

	/* Set defaults and parse the command line. */
	if(fswc_getopts(config, argc, argv)) return(-1);

	/* Open the log file if one was specified. */
	if(config->logfile && fswc_openlog(config)) return(-1);

	/* Go into the background if requested. */
	if(config->background && fswc_background(config)) return(-1);

	/* Save PID of requested. */
	if(config->pidfile && fswc_savepid(config)) return(-1);

	/* Setup signal handlers. */
	if(fswc_setup_signals()) return(-1);
	
	/* Enable FontConfig support in GD */
	if(!gdFTUseFontConfig(1)) DEBUG("gd has no fontconfig support");
	/* Capture the image(s). */
	/* Capture the image. */
	fswc_grab(config);

	/* Close the log file. */
	if(config->logfile) log_close();
	
	/* Free all used memory. */
	fswc_free_config(config);
	free(config);
	
	return(0);
}

