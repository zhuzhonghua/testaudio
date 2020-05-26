#include <SDL.h>

#include <queue>
#include <cmath>
#include <stdio.h>
#include <assert.h>

const int AMPLITUDE = 28000;
const int FREQUENCY = 44100;

struct BeepObject
{
    double freq;
    int samplesLeft;
};

class Beeper
{
private:
    double v;
    std::queue<BeepObject> beeps;
		SDL_AudioDeviceID dev;
public:
    Beeper();
    ~Beeper();
    void beep(double freq, int duration);
    void generateSamples(Sint16 *stream, int length);
    void wait();
};

void audio_callback(void*, Uint8*, int);

Beeper::Beeper()
{
    SDL_AudioSpec desiredSpec;

    desiredSpec.freq = FREQUENCY;
    desiredSpec.format = AUDIO_S16SYS;
    desiredSpec.channels = 1;
    desiredSpec.samples = 2048;
    desiredSpec.callback = audio_callback;
    desiredSpec.userdata = this;

    SDL_AudioSpec obtainedSpec;

		dev = SDL_OpenAudioDevice(NULL, 0, &desiredSpec, &obtainedSpec, SDL_AUDIO_ALLOW_ANY_CHANGE);
    // you might want to look for errors here
    //SDL_OpenAudio(&desiredSpec, &obtainedSpec);

    // start play audio
    //SDL_PauseAudio(0);
		SDL_PauseAudioDevice(dev, 0);
}

Beeper::~Beeper()
{
		SDL_CloseAudioDevice(dev);
    //SDL_CloseAudio();
}

void Beeper::generateSamples(Sint16 *stream, int length)
{
    int i = 0;
    while (i < length) {

        if (beeps.empty()) {
            while (i < length) {
                stream[i] = 0;
                i++;
            }
            return;
        }
        BeepObject& bo = beeps.front();

        int samplesToDo = std::min(i + bo.samplesLeft, length);
        bo.samplesLeft -= samplesToDo - i;

        while (i < samplesToDo) {
            stream[i] = AMPLITUDE * std::sin(v * 2 * M_PI / FREQUENCY);
            i++;
            v += bo.freq;
        }

        if (bo.samplesLeft == 0) {
            beeps.pop();
        }
    }
}

void Beeper::beep(double freq, int duration)
{
    BeepObject bo;
    bo.freq = freq;
    bo.samplesLeft = duration * FREQUENCY / 1000;

		SDL_LockAudioDevice(dev);
    //SDL_LockAudio();
    beeps.push(bo);
    //SDL_UnlockAudio();
		SDL_UnlockAudioDevice(dev);
}

void Beeper::wait()
{
    int size;
    do {
        SDL_Delay(20);
        SDL_LockAudioDevice(dev);
        size = beeps.size();
        SDL_UnlockAudioDevice(dev);
    } while (size > 0);

}

void audio_callback(void *_beeper, Uint8 *_stream, int _length)
{
		SDL_memset(_stream, 0, _length);
    Sint16 *stream = (Sint16*) _stream;
    int length = _length / 2;
    Beeper* beeper = (Beeper*) _beeper;

    beeper->generateSamples(stream, length);
}

int main(int argc, char* argv[])
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);

    int duration = 10000;
    double Hz = 440;

    Beeper b;
    b.beep(Hz, duration);
    b.wait();

    return 0;
}