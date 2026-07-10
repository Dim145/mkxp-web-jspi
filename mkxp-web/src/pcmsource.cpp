/*
** pcmsource.cpp  (WEB PORT addition)
**
** On the Emscripten/WASM build, mkxp's streaming SDL_sound path (createSDLSource,
** which decodes WAV/MP3/FLAC/etc.) and the FluidSynth MIDI path are compiled out --
** only Vorbis/OGG can be decoded (see alstream.cpp / soundemitter.cpp tryRead under
** #ifdef __EMSCRIPTEN__). That left every .wav / .mp3 asset silent in the browser.
**
** This file adds two ALDataSource factories that decode the WHOLE file up front into
** interleaved signed-16 PCM held in memory, then serve it to OpenAL in STREAM_BUF_SIZE
** chunks (with loop wrap-around for BGM/BGS/ME) or all at once (fillBufferFull for SE):
**   - createWavSource: SDL2's built-in SDL_LoadWAV_RW (+ SDL_AudioCVT to normalise to
**     S16 mono/stereo). No extra dependency.
**   - createMp3Source: minimp3 (single-header, public domain). Compiled only when
**     MKXP_HAVE_MINIMP3 is defined (i.e. src/minimp3_ex.h is present); otherwise it
**     throws so the caller's tryRead falls through gracefully.
**
** Ownership contract mirrors VorbisSource: the returned source OWNS `ops` and closes it
** in its destructor; on a decode error the factory closes `ops` before throwing.
** Decoding wholesale is fine here: game SFX are short, and music tracks comfortably fit
** in memory (the OGG path on this build is effectively wholesale for SE too).
*/

#include "aldatasource.h"
#include "exception.h"

#include <SDL_audio.h>
#include <SDL_rwops.h>

#include <vector>
#include <string.h>
#include <stdint.h>

/* A data source backed by fully-decoded interleaved S16 PCM in memory. */
struct StaticPCMSource : ALDataSource
{
	SDL_RWops &src;             /* owned; closed in dtor (kept only for lifecycle parity) */
	std::vector<int16_t> pcm;   /* interleaved S16 samples */
	int channels;
	int rate;
	ALenum alFormat;
	int frameSize;              /* bytes per frame = 2 * channels */
	size_t totalFrames;
	size_t posFrames;
	bool looped;

	StaticPCMSource(SDL_RWops &ops, std::vector<int16_t> &&data,
	                int chan, int freq, bool loop)
	    : src(ops),
	      pcm(std::move(data)),
	      channels(chan < 1 ? 1 : chan),
	      rate(freq),
	      posFrames(0),
	      looped(loop)
	{
		frameSize   = (int)sizeof(int16_t) * channels;
		alFormat    = chooseALFormat(sizeof(int16_t), channels);
		totalFrames = frameSize ? (pcm.size() / channels) : 0;
	}

	~StaticPCMSource()
	{
		SDL_RWclose(&src);
	}

	int sampleRate()
	{
		return rate;
	}

	void seekToOffset(float seconds)
	{
		if (seconds <= 0)
		{
			posFrames = 0;
			return;
		}

		size_t f = (size_t)(seconds * rate);
		posFrames = (f < totalFrames) ? f : (looped ? 0 : totalFrames);
	}

	Status fillBuffer(AL::Buffer::ID alBuffer)
	{
		if (totalFrames == 0)
		{
			AL::Buffer::uploadData(alBuffer, alFormat, pcm.data(), 0, rate);
			return ALDataSource::EndOfStream;
		}

		size_t chunkFrames = frameSize ? (STREAM_BUF_SIZE / frameSize) : 1;
		if (chunkFrames == 0)
			chunkFrames = 1;

		bool wrapped = false;

		if (posFrames >= totalFrames)
		{
			if (looped)
			{
				posFrames = 0;
				wrapped = true;
			}
			else
			{
				AL::Buffer::uploadData(alBuffer, alFormat, pcm.data(), 0, rate);
				return ALDataSource::EndOfStream;
			}
		}

		size_t framesLeft = totalFrames - posFrames;
		size_t take = (framesLeft < chunkFrames) ? framesLeft : chunkFrames;

		const int16_t *start = pcm.data() + posFrames * (size_t)channels;
		AL::Buffer::uploadData(alBuffer, alFormat, start,
		                       (ALsizei)(take * frameSize), rate);
		posFrames += take;

		if (posFrames >= totalFrames)
		{
			if (looped)
			{
				posFrames = 0;
				return ALDataSource::WrapAround;
			}
			return ALDataSource::EndOfStream;
		}

		return wrapped ? ALDataSource::WrapAround : ALDataSource::NoError;
	}

	int fillBufferFull(AL::Buffer::ID alBuffer)
	{
		ALsizei bytes = (ALsizei)(pcm.size() * sizeof(int16_t));
		AL::Buffer::uploadData(alBuffer, alFormat, pcm.data(), bytes, rate);
		return bytes;
	}

	uint32_t loopStartFrames()
	{
		/* WAV/MP3 carry no LOOPSTART metadata; loop from the top. */
		return 0;
	}

	bool setPitch(float)
	{
		return false;
	}
};

/* ------------------------------------------------------------------ WAV */

ALDataSource *createWavSource(SDL_RWops &ops, bool looped)
{
	SDL_AudioSpec spec;
	Uint8 *wavBuf = 0;
	Uint32 wavLen = 0;

	/* freesrc = 0: keep `ops` open; StaticPCMSource closes it in its dtor. */
	if (!SDL_LoadWAV_RW(&ops, 0, &spec, &wavBuf, &wavLen))
	{
		SDL_RWclose(&ops);
		throw Exception(Exception::MKXPError,
		                "SDL_LoadWAV: %s", SDL_GetError());
	}

	int outChannels = (spec.channels > 2) ? 2 : (spec.channels < 1 ? 1 : spec.channels);

	SDL_AudioCVT cvt;
	int cvtNeeded = SDL_BuildAudioCVT(&cvt,
	                                  spec.format, spec.channels, spec.freq,
	                                  AUDIO_S16SYS, outChannels, spec.freq);

	std::vector<int16_t> pcm;

	if (cvtNeeded < 0)
	{
		SDL_FreeWAV(wavBuf);
		SDL_RWclose(&ops);
		throw Exception(Exception::MKXPError,
		                "SDL_BuildAudioCVT failed for WAV: %s", SDL_GetError());
	}
	else if (cvtNeeded == 0)
	{
		/* Already S16SYS with the desired channel count. */
		pcm.resize(wavLen / sizeof(int16_t));
		if (wavLen)
			memcpy(pcm.data(), wavBuf, wavLen);
	}
	else
	{
		cvt.len = (int)wavLen;
		cvt.buf = (Uint8 *)SDL_malloc((size_t)cvt.len * cvt.len_mult);

		if (!cvt.buf)
		{
			SDL_FreeWAV(wavBuf);
			SDL_RWclose(&ops);
			throw Exception(Exception::MKXPError, "Out of memory decoding WAV");
		}

		memcpy(cvt.buf, wavBuf, wavLen);

		if (SDL_ConvertAudio(&cvt) < 0)
		{
			SDL_free(cvt.buf);
			SDL_FreeWAV(wavBuf);
			SDL_RWclose(&ops);
			throw Exception(Exception::MKXPError,
			                "SDL_ConvertAudio failed for WAV: %s", SDL_GetError());
		}

		pcm.resize((size_t)cvt.len_cvt / sizeof(int16_t));
		if (cvt.len_cvt)
			memcpy(pcm.data(), cvt.buf, cvt.len_cvt);
		SDL_free(cvt.buf);
	}

	SDL_FreeWAV(wavBuf);

	return new StaticPCMSource(ops, std::move(pcm), outChannels, spec.freq, looped);
}

/* ------------------------------------------------------------------ MP3 */

#ifdef MKXP_HAVE_MINIMP3

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3_ex.h"

ALDataSource *createMp3Source(SDL_RWops &ops, bool looped)
{
	/* Slurp the whole file into memory, then decode all frames. */
	Sint64 size = SDL_RWsize(&ops);
	if (size <= 0)
	{
		SDL_RWclose(&ops);
		throw Exception(Exception::MKXPError, "MP3: empty or unseekable stream");
	}

	std::vector<uint8_t> raw((size_t)size);
	SDL_RWseek(&ops, 0, RW_SEEK_SET);
	size_t got = SDL_RWread(&ops, raw.data(), 1, (size_t)size);

	if (got == 0)
	{
		SDL_RWclose(&ops);
		throw Exception(Exception::MKXPError, "MP3: read failed");
	}

	mp3dec_t mp3d;
	mp3dec_file_info_t info;
	memset(&info, 0, sizeof(info));

	if (mp3dec_load_buf(&mp3d, raw.data(), got, &info, 0, 0) || !info.buffer)
	{
		if (info.buffer)
			free(info.buffer);
		SDL_RWclose(&ops);
		throw Exception(Exception::MKXPError, "minimp3: cannot decode mp3");
	}

	int channels = info.channels > 2 ? 2 : (info.channels < 1 ? 1 : info.channels);

	std::vector<int16_t> pcm((size_t)info.samples);
	if (info.samples > 0)
		memcpy(pcm.data(), info.buffer, (size_t)info.samples * sizeof(mp3d_sample_t));

	free(info.buffer);

	return new StaticPCMSource(ops, std::move(pcm), channels, (int)info.hz, looped);
}

#else

ALDataSource *createMp3Source(SDL_RWops &ops, bool /*looped*/)
{
	SDL_RWclose(&ops);
	throw Exception(Exception::MKXPError,
	                "MP3 decoding not compiled in (MKXP_HAVE_MINIMP3 unset)");
}

#endif
