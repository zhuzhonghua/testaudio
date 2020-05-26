/*
 * SimpleMixer 1.1
 *
 * Simple sound mixing example for SDL.
 *
 * (C) David Olofson, 2003
 */

#include <SDL.h>
#include <stdio.h>

#define	SM_SOUNDS	4
#define	SM_VOICES	4

//The window we'll be rendering to
SDL_Window* gWindow = NULL;

//The window renderer
SDL_Renderer* gRenderer = NULL;

SDL_AudioDeviceID dev;

struct SM_sound
{
	Uint8	*data;
	Uint32	length;
};


struct SM_voice
{
	Sint16	*data;
	int	length;
	int	position;
	int	l_vol;
	int	r_vol;
};


SM_sound sounds[SM_SOUNDS];
SM_voice voices[SM_VOICES];
SDL_AudioSpec audiospec;
int die = 0;


void sm_play(unsigned voice, unsigned sound, float lvol, float rvol)
{
	if(voice >= SM_VOICES || sound >= SM_SOUNDS)
		return;

	/* Stop voice */
	voices[voice].data = NULL;

	/* Reprogram */
	voices[voice].length = sounds[sound].length / 2;
	voices[voice].position = 0;
	voices[voice].l_vol = (int)(lvol * 256.0);
	voices[voice].r_vol = (int)(rvol * 256.0);

	/* Start! */
	voices[voice].data = (Sint16*)(sounds[sound].data);
}


static void sm_mixer(void *ud, Uint8 *stream, int len)
{
	int vi, s;
	Sint16 *buf = (Sint16 *)stream;

	/* Clear the buffer */
	memset(buf, 0, len);

	/* 2 channels, 2 bytes/sample = 4 bytes/frame */
        len /= 4;

	/* For each voice... */
	for(vi = 0; vi < SM_VOICES; ++vi)
	{
		SM_voice *v = &voices[vi];
		if(!v->data)
			continue;

		/* For each sample... */
		for(s = 0; s < len; ++s)
		{
			if(v->position >= v->length)
			{
				v->data = NULL;
				break;
			}
			buf[s * 2] += v->data[v->position] * v->l_vol >> 8;
			buf[s * 2 + 1] += v->data[v->position] * v->r_vol >> 8;
			++v->position;
		}
	}
}


int sm_open(void)
{
	SDL_AudioSpec as;
	SDL_zero(as);
	
	memset(sounds, 0, sizeof(sounds));
	memset(voices, 0, sizeof(voices));

	as.freq = 44100;
	as.format = AUDIO_S16SYS;
	as.channels = 2;
	as.samples = 1024;
	as.callback = sm_mixer;
	if((dev=SDL_OpenAudioDevice(NULL, 0, &as, &audiospec, SDL_AUDIO_ALLOW_ANY_CHANGE)) < 0)
		return -3;

	if(audiospec.format != AUDIO_S16SYS)
		return -4;

	SDL_PauseAudioDevice(dev, 0);; // start playing sound
	return 0;
}


void sm_close(void)
{
	int i;
	SDL_PauseAudioDevice(dev, 1);
	for(i = 0; i < SM_VOICES; ++i)
		voices[i].data = NULL;
	SDL_CloseAudioDevice(dev);
	for(i = 0; i < SM_SOUNDS; ++i)
		SDL_FreeWAV(sounds[i].data);
	memset(sounds, 0, sizeof(sounds));
	memset(voices, 0, sizeof(voices));
}

int sm_load(int sound, const char *file)
{
	int failed = 0;
	SDL_AudioSpec spec;
	
	if(sounds[sound].data)
		SDL_FreeWAV(sounds[sound].data);
	sounds[sound].data = NULL;
	
	if(SDL_LoadWAV(file, &spec, &sounds[sound].data,
			&sounds[sound].length) == NULL)
		return -1;
	if(spec.freq != 44100)
		fprintf(stderr, "WARNING: File '%s' is not 44.1 kHz."
				" Might sound weird...\n", file);
	if(spec.channels != 1)
	{
		fprintf(stderr, "Only mono sounds are supported!\n");
		failed = 1;
	}
	
	if(failed)
	{
		SDL_FreeWAV(sounds[sound].data);
		sounds[sound].data = NULL;
		return -2;
	}
	return 0;
}


void breakhandler(int a)
{
	die = 1;
}


int main(int argc, char *argv[])
{
	const char *bd = "#...#...#...#..##...#...#...#.##";
	const char *cl = "....#..*....#....*..#....*..#.**";
	const char *cb = "#...*..#..*...#.#...*..#..*..*#*";
	const char *hh = "..#...#...#...#...#...#...#...#.";
	int res;
	Uint32 step = 0;
	Sint32 timer;
	
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0)
		return -1;

	gWindow = SDL_CreateWindow( "simplemixer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 960, 640, SDL_WINDOW_SHOWN );
	gRenderer = SDL_CreateRenderer( gWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC );
	
	if(sm_open() < 0)
	{
		fprintf(stderr, "Couldn't start mixer!\n");
		SDL_Quit();
		return -1;
	}

	res = 0;
	res |= sm_load(0, "808-bassdrum.wav");
	res |= sm_load(1, "808-clap.wav");
	res |= sm_load(2, "808-cowbell.wav");
	res |= sm_load(3, "808-hihat.wav");
	if(res)
	{
		sm_close();
		SDL_Quit();
		fprintf(stderr, "Couldn't load sounds!\n");
		return -1;
	}

	/*
	 * Of course, playing this stuff would be
	 * much better done in the audio callback,
	 * since the timing out here jitters a lot.
	 *
	 * However, this is just a fun hack to get
	 * the thing to make some sound, without
	 * slapping in a game or something. :-)
	 */
	SDL_Delay(200);
	timer = (Sint32)SDL_GetTicks();
	while(!die)
	{
		SDL_Event event;
		SDL_PollEvent(&event);
		switch (event.type) {
		case SDL_QUIT:
			die = true;
			break;
		}
		if('#' == bd[step])
			sm_play(0, 0, 1.0, 1.0);

		if('#' == cl[step])
			sm_play(1, 1, 0.6, 0.5);
		else if('*' == cl[step])
			sm_play(1, 1, 0.2, 0.3);

		if('#' == cb[step])
			sm_play(2, 2, 0.3, 0.2);
		else if('*' == cb[step])
			sm_play(2, 2, 0.1, 0.2);

		if('#' == hh[step])
			sm_play(3, 3, 0.3, 0.4);

		step = (step + 1) % 32;

		timer += 120;
		while(((Sint32)SDL_GetTicks() - timer) < 0)
			SDL_Delay(10);
	}

	sm_close();
	SDL_Quit();
	return 0;
}
