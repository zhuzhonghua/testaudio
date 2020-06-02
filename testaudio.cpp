#include <SDL.h>

#include <queue>
#include <cmath>
#include <stdio.h>
#include <assert.h>
#include <vector>
#include <time.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/dict.h>
#include <libswresample/swresample.h>
};

#define SDL_AUDIO_BUFFER_SIZE 4096
const int AMPLITUDE = 28000;
const int SAMPLE_RATE = 44100;
#define MAX_AUDIO_FRAME_SIZE 192000
SDL_Texture* tex;
SDL_Renderer* renderer;

int audio_decode_frame_private(AVCodecContext *aCodecCtx, AVPacket* packet, uint8_t *audio_buf, int buf_size);

class SamplesQueue{
public:
	SDL_mutex *mutex;
	SDL_cond *cond;
	std::vector<std::vector<short> > samples;

	SamplesQueue()
	{
		mutex = SDL_CreateMutex();
  	cond = SDL_CreateCond();
	}

	int put_packet(AVCodecContext *aCodecCtx, AVPacket *packet)
	{
		if(av_dup_packet(packet) < 0) {
    	return -1;
	  }
	    
	  SDL_LockMutex(mutex);

	  uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
		unsigned int audio_buf_size = 0;
  	unsigned int audio_buf_index = 0;
  	audio_buf_size = audio_decode_frame_private(aCodecCtx, packet, audio_buf, sizeof(audio_buf));

  	short* samples_data = (short*)audio_buf;
  	int samples_len = audio_buf_size / 2;
  	samples.emplace_back(samples_data, samples_data+samples_len);

	  SDL_CondSignal(cond);

	  SDL_UnlockMutex(mutex);

	  //render(samples_data, samples_len);
	  return 0;
	}

	int get_sample(uint8_t* buf, int size)
	{
		SDL_LockMutex(mutex);
		if (samples.size() <= 0)
		{
			SDL_CondWait(cond, mutex);
		}
		std::vector<short> sample_data = samples.front();
		int len = sample_data.size()*sizeof(short);
		memcpy(buf, sample_data.data(), len);
		samples.erase(samples.begin());
		SDL_UnlockMutex(mutex);

		return len;
	}

	void render(short* samples_data, int samples_len)
	{
		static int x = 0;
		static int y = 0;
		const int H = 200;

		SDL_SetRenderTarget(renderer, tex);
		SDL_SetRenderDrawColor( renderer, 0x00, 0xFF, 0xFF, 0xFF );
	  for (int i = 0; i < samples_len; i++)
	  {
	  	short one_sample = samples_data[i];
	  	
	  	int sampleH = ((float)one_sample / 32768.0f)*H;
	  	//SDL_Log("render sampleH %d one_sample %d\n", sampleH, one_sample);
	  	if (sampleH > 0) 
	  	{		  	
				SDL_Rect fillRect = { x++, y+(H - sampleH)/2, 1, sampleH};  
		  	SDL_RenderFillRect( renderer, &fillRect );		
	  	}
	
	  	if (x >= 480)
	  	{
	  		x = 0;
	  		y += H;
	  	}
	  }

		SDL_SetRenderTarget(renderer, NULL);		
	}
};


struct PacketQueue {
	std::vector<AVPacket> pkt;

  int size;
  SDL_mutex *mutex;
  SDL_cond *cond;
};

AVFrame wanted_frame;
PacketQueue audioq;
SamplesQueue samplesq;
int quit = 0;

SDL_mutex* texMutex;


void packet_queue_init(PacketQueue *q)
{
	q->size = 0;
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) 
{
  AVPacketList *pkt1;
  if(av_dup_packet(pkt) < 0) {
    return -1;
  }
    
  SDL_LockMutex(q->mutex);
  
  q->pkt.push_back(*pkt);  
  q->size += pkt->size;


  SDL_CondSignal(q->cond);
  
  //SDL_Log("queue size %d\n", q->pkt.size());

  SDL_UnlockMutex(q->mutex);
  return 0;
}

static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
  int ret;
  
  SDL_LockMutex(q->mutex);
  
  for(;;) {
    
    if(quit) {
      ret = -1;
      break;
    }

    if (q->pkt.size() > 0) {      
      *pkt = q->pkt.front();
      q->size -= pkt->size;
      q->pkt.erase(q->pkt.begin());
      ret = 1;
      break;
    } else if (!block) {
      ret = 0;
      break;
    } else {
      SDL_CondWait(q->cond, q->mutex);
    }
  }
  SDL_UnlockMutex(q->mutex);
  return ret;
}


int audio_decode_frame_private(AVCodecContext *aCodecCtx, AVPacket* packet, uint8_t *audio_buf, int buf_size)
{

  SwrContext *swr_ctx = NULL;
  int        	convert_all = 0;
  int 				audio_buf_index = 0;

  // TO delete av_frame_free(&frame);
	AVFrame* frame = av_frame_alloc();
  int pkt_size = packet->size;
  while(pkt_size > 0)
  {
  	int got_frame = 0;
  	int decode_len = avcodec_decode_audio4(aCodecCtx, frame, &got_frame, packet);
  	if (decode_len < 0) //解码出错
  	{
  		assert(0);
  		return -1;
  	}

  	if (got_frame)
  	{
  		if (frame->channels > 0 && frame->channel_layout == 0)
      {
        //获取默认布局，默认应该了stereo吧？
        frame->channel_layout = av_get_default_channel_layout(frame->channels);
      }
      else if (frame->channels == 0 && frame->channel_layout > 0)
      {
        frame->channels = av_get_channel_layout_nb_channels(frame->channel_layout);
      }

      if (swr_ctx != NULL)
      {
        swr_free(&swr_ctx);
        swr_ctx = NULL;
      }

      swr_ctx = swr_alloc_set_opts(NULL, wanted_frame.channel_layout,
						                      (AVSampleFormat)wanted_frame.format,
						                      wanted_frame.sample_rate, frame->channel_layout,
						                      (AVSampleFormat)frame->format, frame->sample_rate, 0, NULL);

      //初始化
      if (swr_ctx == NULL || swr_init(swr_ctx) < 0)
      {
      	assert(0);
      	break;
      }

      int convert_len = swr_convert(swr_ctx, 
                                &audio_buf + audio_buf_index,
                                MAX_AUDIO_FRAME_SIZE,
                                (const uint8_t **)frame->data, 
                                frame->nb_samples);

      pkt_size -= decode_len;
      audio_buf_index += convert_len;
      convert_all += convert_len;
  	}
  }

  swr_free(&swr_ctx);
  av_frame_free(&frame);

  return wanted_frame.channels * convert_all * av_get_bytes_per_sample((AVSampleFormat)wanted_frame.format);
}


void render_to_tex(AVCodecContext *aCodecCtx, AVPacket* pkt)
{
	static int x = 0;
	static int y = 0;
	const int H = 20;

	//AVPacket packet = *pkt;
//
	//uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	//unsigned int audio_buf_size = 0;
  //unsigned int audio_buf_index = 0;
  //audio_buf_size = audio_decode_frame_private(aCodecCtx, &packet, audio_buf, sizeof(audio_buf));
//
  //short* samples = (short*)audio_buf;
  //int samples_len = audio_buf_size / 2;
//
//
	SDL_SetRenderTarget(renderer, tex);
	SDL_SetRenderDrawColor( renderer, 0x00, 0x00, 0xFF, 0xFF );
	for (int i = 0; i < 500; i++)
	{
		int sampleH = rand()%20;
		SDL_Rect fillRect = { x++, y+(20 - sampleH)/2, 1, rand()%20};
  	SDL_RenderFillRect( renderer, &fillRect );	

  	if (x >= 640)
  	{
  		x = 0;
  		y += H;
  	}
	}
  //for (int i = 0; i < samples_len; i++)
  //{
  //	short one_sample = samples[i];
  //	int sampleH = (one_sample / 32768.0f)*H;
  //	SDL_Rect fillRect = { x++, y, 1, sampleH};  
  //	SDL_RenderFillRect( renderer, &fillRect );	
//
  //	if (x >= 480)
  //	{
  //		x = 0;
  //		y += H;
  //	}
  //}

	SDL_SetRenderTarget(renderer, NULL);
}

int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size) 
{
	AVPacket   packet;

  if (packet_queue_get(&audioq, &packet, 1) < 0)
  {
		assert(0);
		return -1;
  }
  return audio_decode_frame_private(aCodecCtx, &packet, audio_buf, buf_size);
}

void audio_callback_new(void *user_data, Uint8 *stream, int len)
{
	static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
  static unsigned int audio_buf_size = 0;
  static unsigned int audio_buf_index = 0;

	SDL_memset(stream, 0, len);

	while(len > 0) {
  	if(audio_buf_index >= audio_buf_size) {
  		//samplesq.get_sample(audio_buf, sizeof(audio_buf));
      /* We have already sent all our data; get more */
      int audio_size = samplesq.get_sample(audio_buf, sizeof(audio_buf)); //audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));
      if(audio_size < 0) {
      	assert(0);
				/* If error, output silence */
				audio_buf_size = 1024; // arbitrary?
				memset(audio_buf, 0, audio_buf_size);
      } else {
				audio_buf_size = audio_size;
      }
      audio_buf_index = 0;
    }
    int len1 = audio_buf_size - audio_buf_index;
    if(len1 > len)
      len1 = len;
		
		SDL_MixAudioFormat(stream, (uint8_t *)audio_buf + audio_buf_index, AUDIO_S16SYS, len, SDL_MIX_MAXVOLUME);	

    len -= len1;
    stream += len1;
    audio_buf_index += len1;

  }
}

void audio_callback(void *user_data, Uint8 *stream, int len)
{
	//printf("callback tick %d\n", SDL_GetTicks());
	AVCodecContext *aCodecCtx = (AVCodecContext *)user_data;

	static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
  static unsigned int audio_buf_size = 0;
  static unsigned int audio_buf_index = 0;

	SDL_memset(stream, 0, len);

  while(len > 0) {
  	if(audio_buf_index >= audio_buf_size) {
      /* We have already sent all our data; get more */
      int audio_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));
      if(audio_size < 0) {
      	assert(0);
				/* If error, output silence */
				audio_buf_size = 1024; // arbitrary?
				memset(audio_buf, 0, audio_buf_size);
      } else {
				audio_buf_size = audio_size;
      }
      audio_buf_index = 0;
    }
    int len1 = audio_buf_size - audio_buf_index;
    if(len1 > len)
      len1 = len;
		
		SDL_MixAudioFormat(stream, (uint8_t *)audio_buf + audio_buf_index, AUDIO_S16SYS, len, SDL_MIX_MAXVOLUME);	

    len -= len1;
    stream += len1;
    audio_buf_index += len1;

  }
}


int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: testaudio <file>\n");
		exit(1);
	}

	srand (time(NULL));
	// Register all formats and codecs
	av_register_all();

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
			SDL_Log("Could not initialize SDL - %s\n", SDL_GetError());
			exit(1);
	}

	SDL_Window 			*window;
	// Make a screen to put our video
	window = SDL_CreateWindow(
					"testaudio",
					SDL_WINDOWPOS_UNDEFINED,
					SDL_WINDOWPOS_UNDEFINED,
					640,
					480,
					0
			);

	renderer = SDL_CreateRenderer(window, -1, 0);

	tex = SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,640,480);
	texMutex = SDL_CreateMutex();

	//SDL_SetRenderTarget(renderer, tex);
	//SDL_Rect fillRect = { 0, 0, 100, 100 };
  //SDL_SetRenderDrawColor( renderer, 0xFF, 0x00, 0x00, 0xFF );        
  //SDL_RenderFillRect( renderer, &fillRect );
	//SDL_SetRenderTarget(renderer, NULL);

	AVFormatContext *pFormatCtx = NULL;
	AVPacket 				packet;
	int             audioStream = 0;
	AVCodecContext  *aCodecCtx = NULL;
  AVCodec         *aCodec = NULL;
  
  // Open video file
	if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0)
			return -1; // Couldn't open file

	// Retrieve stream information
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
			return -1; // Couldn't find stream information

	// Dump information about file onto standard error
	av_dump_format(pFormatCtx, 0, argv[1], 0);

	audioStream=-1;
	for(int i=0; i<pFormatCtx->nb_streams; i++) {
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO && audioStream < 0) {
			audioStream = i;
		}
	}
	if(audioStream==-1)
		return -1;

	// Get a pointer to the codec context for the video stream
	aCodecCtx = pFormatCtx->streams[audioStream]->codec;
	// Find the decoder for the video stream
	aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
	if (aCodec == NULL) {
			fprintf(stderr, "Unsupported codec!\n");
			return -1; // Codec not found
	}

  SDL_AudioSpec want;
  want.freq = aCodecCtx->sample_rate; // number of samples per second
  want.format = AUDIO_S16SYS; // sample type (here: signed short i.e. 16 bit)
  want.channels = aCodecCtx->channels; // only one channel
  want.silence = 0;
  want.samples = SDL_AUDIO_BUFFER_SIZE; // buffer-size
  want.callback = audio_callback_new; // function SDL calls periodically to refill the buffer
  want.userdata = aCodecCtx; // counter, keeping track of current sample number

  SDL_AudioSpec have;
	SDL_AudioDeviceID dev;
  if((dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_ANY_CHANGE)) != 0)
		SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to open audio: %s", SDL_GetError());
  if(want.format != have.format) SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to get the desired AudioSpec");

  SDL_Log("want freq %d sample %d, have freq %d sample %d\n", want.freq, want.samples, have.freq, have.samples);

	//设置参数，供解码时候用, swr_alloc_set_opts的in部分参数
  wanted_frame.format         = AV_SAMPLE_FMT_S16;
  wanted_frame.sample_rate    = have.freq / 2;
  wanted_frame.channel_layout = av_get_default_channel_layout(have.channels);
  wanted_frame.channels       = have.channels;

	avcodec_open2(aCodecCtx, aCodec, NULL);
	
	// audio_st = pFormatCtx->streams[index]
  //packet_queue_init(&audioq);

  SDL_PauseAudioDevice(dev, 0);; // start playing sound


  while(!quit) 
  {
  	if (av_read_frame(pFormatCtx, &packet) >= 0) 
  	{
  		if(packet.stream_index == audioStream) 
	  	{
	  		//packet_queue_put(&audioq, &packet);
	  		//render_to_tex(aCodecCtx, &packet);
	  		samplesq.put_packet(aCodecCtx, &packet);
	  	}
	  	else
	  	{
	  		av_free_packet(&packet);
	  	}
  	}
  	

  	SDL_Event event;
  	while(SDL_PollEvent(&event))
  	{
  		SDL_PollEvent(&event);
			switch (event.type) {
			case SDL_QUIT:			
				SDL_Log("quit tick %d\n", SDL_GetTicks());
				quit = 1;
				
				break;
			}
  	}
  	
  	SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
		SDL_RenderClear(renderer);

		SDL_RenderCopy(renderer, tex, NULL, NULL);

    SDL_RenderPresent(renderer);

		SDL_Delay(10);
  }

	SDL_Log("app quit tick %d\n", SDL_GetTicks());

	SDL_PauseAudioDevice(dev, 1); // stop playing sound
  SDL_CloseAudioDevice(dev);
  SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
  return 0;
}