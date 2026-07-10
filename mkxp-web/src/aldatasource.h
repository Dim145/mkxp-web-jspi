/*
** aldatasource.h
**
** This file is part of mkxp.
**
** Copyright (C) 2014 Jonas Kulla <Nyocurio@gmail.com>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef ALDATASOURCE_H
#define ALDATASOURCE_H

#include "al-util.h"

struct ALDataSource
{
	enum Status
	{
		NoError,
		EndOfStream,
		WrapAround,
		Error
	};

	virtual ~ALDataSource() {}

	/* Read/process next chunk of data, and attach it
	 * to provided AL buffer */
	virtual Status fillBuffer(AL::Buffer::ID alBuffer) = 0;

	/* Read everything into the provided buffer */
	virtual int fillBufferFull(AL::Buffer::ID alBuffer) {
		return 0;
	};

	virtual int sampleRate() = 0;

	/* If the source doesn't support seeking, it will
	 * reset back to the beginning */
	virtual void seekToOffset(float seconds) = 0;

	/* The frame count right after wrap around */
	virtual uint32_t loopStartFrames() = 0;

	/* Returns false if not supported */
	virtual bool setPitch(float value) = 0;
};

#ifndef __EMSCRIPTEN__
ALDataSource *createSDLSource(SDL_RWops &ops,
                              const char *extension,
			                  uint32_t maxBufSize,
			                  bool looped);
#endif

ALDataSource *createVorbisSource(SDL_RWops &ops,
                                 bool looped);

ALDataSource *createMidiSource(SDL_RWops &ops,
                               bool looped);

/* WEB PORT: whole-file PCM decoders used on Emscripten, where the streaming
 * SDL_sound path is compiled out and only Vorbis would otherwise decode. */
ALDataSource *createWavSource(SDL_RWops &ops,
                              bool looped);

ALDataSource *createMp3Source(SDL_RWops &ops,
                              bool looped);

#endif // ALDATASOURCE_H
