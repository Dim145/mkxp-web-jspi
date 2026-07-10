/*
** input.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 Jonas Kulla <Nyocurio@gmail.com>
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

#include "input.h"
#include "sharedstate.h"
#include "eventthread.h"
#include "keybindings.h"
#include "exception.h"
#include "util.h"

#include <SDL_scancode.h>
#include <SDL_mouse.h>

#include <vector>
#include <string.h>
#include <assert.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
/* WEB PORT: input setters called by JS key listeners in index.html. Defined in
 * THIS translation unit — the SAME one whose KbBinding::sourceActive READS
 * EventThread::keyStates — to guarantee writes land in the array the game reads.
 * (Defining them in another TU resolved to a different keyStates instance, so
 * keyboard input never registered.) web_get_scancode is a round-trip probe. */
extern "C" {
EMSCRIPTEN_KEEPALIVE void web_set_scancode(int scancode, int down) {
	if (scancode >= 0 && scancode < SDL_NUM_SCANCODES)
		EventThread::keyStates[scancode] = (down != 0);
}
EMSCRIPTEN_KEEPALIVE int web_get_scancode(int scancode) {
	if (scancode >= 0 && scancode < SDL_NUM_SCANCODES)
		return EventThread::keyStates[scancode] ? 1 : 0;
	return -1;
}
EMSCRIPTEN_KEEPALIVE void web_set_mouse(int x, int y, int leftDown) {
	EventThread::mouseState.x = x;
	EventThread::mouseState.y = y;
	EventThread::mouseState.buttons[SDL_BUTTON_LEFT] = (leftDown != 0);
}
EMSCRIPTEN_KEEPALIVE int web_binding_count() {
	return shState ? shState->input().webBindingCount() : -1;
}
}
#endif

#define BUTTON_CODE_COUNT 24

struct ButtonState
{
	bool pressed;
	bool triggered;
	bool repeated;

	ButtonState()
		: pressed(false),
		  triggered(false),
		  repeated(false)
	{}
};

struct KbBindingData
{
	SDL_Scancode source;
	Input::ButtonCode target;
};

struct Binding
{
	Binding(Input::ButtonCode target = Input::None)
		: target(target)
	{}

	virtual bool sourceActive() const = 0;
	virtual bool sourceRepeatable() const = 0;

	Input::ButtonCode target;
};

/* Keyboard binding */
struct KbBinding : public Binding
{
	KbBinding() {}

	KbBinding(const KbBindingData &data)
		: Binding(data.target),
		  source(data.source)
	{}

	bool sourceActive() const
	{
		/* Special case aliases */
		if (source == SDL_SCANCODE_LSHIFT)
			return EventThread::keyStates[source]
			    || EventThread::keyStates[SDL_SCANCODE_RSHIFT];

		if (source == SDL_SCANCODE_RETURN)
			return EventThread::keyStates[source]
			    || EventThread::keyStates[SDL_SCANCODE_KP_ENTER];

		return EventThread::keyStates[source];
	}

	bool sourceRepeatable() const
	{
		return (source >= SDL_SCANCODE_A     && source <= SDL_SCANCODE_0)    ||
		       (source >= SDL_SCANCODE_RIGHT && source <= SDL_SCANCODE_UP)   ||
		       (source >= SDL_SCANCODE_F1    && source <= SDL_SCANCODE_F12);
	}

	SDL_Scancode source;
};

/* Joystick button binding */
struct JsButtonBinding : public Binding
{
	JsButtonBinding() {}

	bool sourceActive() const
	{
		return EventThread::joyState.buttons[source];
	}

	bool sourceRepeatable() const
	{
		return true;
	}

	uint8_t source;
};

/* Joystick axis binding */
struct JsAxisBinding : public Binding
{
	JsAxisBinding() {}

	JsAxisBinding(uint8_t source,
	              AxisDir dir,
	              Input::ButtonCode target)
	    : Binding(target),
	      source(source),
	      dir(dir)
	{}

	bool sourceActive() const
	{
		int val = EventThread::joyState.axes[source];

		if (dir == Negative)
			return val < -JAXIS_THRESHOLD;
		else /* dir == Positive */
			return val > JAXIS_THRESHOLD;
	}

	bool sourceRepeatable() const
	{
		return true;
	}

	uint8_t source;
	AxisDir dir;
};

/* Joystick hat binding */
struct JsHatBinding : public Binding
{
	JsHatBinding() {}

	JsHatBinding(uint8_t source,
	              uint8_t pos,
	              Input::ButtonCode target)
	    : Binding(target),
	      source(source),
	      pos(pos)
	{}

	bool sourceActive() const
	{
		/* For a diagonal input accept it as an input for both the axes */
		return (pos & EventThread::joyState.hats[source]) != 0;
	}

	bool sourceRepeatable() const
	{
		return true;
	}

	uint8_t source;
	uint8_t pos;
};

/* Mouse button binding */
struct MsBinding : public Binding
{
	MsBinding() {}

	MsBinding(int buttonIndex,
	          Input::ButtonCode target)
	    : Binding(target),
	      index(buttonIndex)
	{}

	bool sourceActive() const
	{
		return EventThread::mouseState.buttons[index];
	}

	bool sourceRepeatable() const
	{
		return false;
	}

	int index;
};

/* Not rebindable */
static const KbBindingData staticKbBindings[] =
{
	{ SDL_SCANCODE_LSHIFT, Input::Shift },
	{ SDL_SCANCODE_RSHIFT, Input::Shift },
	{ SDL_SCANCODE_LCTRL,  Input::Ctrl  },
	{ SDL_SCANCODE_RCTRL,  Input::Ctrl  },
	{ SDL_SCANCODE_LALT,   Input::Alt   },
	{ SDL_SCANCODE_RALT,   Input::Alt   },
	{ SDL_SCANCODE_F5,     Input::F5    },
	{ SDL_SCANCODE_F6,     Input::F6    },
	{ SDL_SCANCODE_F7,     Input::F7    },
	{ SDL_SCANCODE_F8,     Input::F8    },
	{ SDL_SCANCODE_F9,     Input::F9    },
	/* WEB PORT: the configurable game-control bindings (defaultKbBindings +
	 * RGSS1 C->C) are delivered via bindingUpdateMsg, which is never received in
	 * the single-threaded emscripten build (no event thread), leaving the game
	 * with NO movement/confirm keys. Bake the essential controls in as static
	 * bindings so keyboard input always works. */
	{ SDL_SCANCODE_LEFT,   Input::Left  },
	{ SDL_SCANCODE_RIGHT,  Input::Right },
	{ SDL_SCANCODE_UP,     Input::Up    },
	{ SDL_SCANCODE_DOWN,   Input::Down  },
	{ SDL_SCANCODE_C,      Input::C     },
	{ SDL_SCANCODE_SPACE,  Input::C     },
	{ SDL_SCANCODE_RETURN, Input::C     },
	{ SDL_SCANCODE_X,      Input::B     },
	{ SDL_SCANCODE_ESCAPE, Input::B     },
	{ SDL_SCANCODE_KP_0,   Input::B     },
	{ SDL_SCANCODE_Z,      Input::A     },
	{ SDL_SCANCODE_D,      Input::Z     },
	{ SDL_SCANCODE_Q,      Input::L     },
	{ SDL_SCANCODE_W,      Input::R     },
	{ SDL_SCANCODE_A,      Input::X     },
	{ SDL_SCANCODE_S,      Input::Y     }
};

static elementsN(staticKbBindings);

/* Maps ButtonCode enum values to indices
 * in the button state array */
static const int mapToIndex[] =
{
	0, 0,
	1, 0, 2, 0, 3, 0, 4, 0,
	0,
	5, 6, 7, 8, 9, 10, 11, 12,
	0, 0,
	13, 14, 15,
	0,
	16, 17, 18, 19, 20,
	0, 0, 0, 0, 0, 0, 0, 0,
	21, 22, 23
};

static elementsN(mapToIndex);

static const Input::ButtonCode dirs[] =
{ Input::Down, Input::Left, Input::Right, Input::Up };

static const int dirFlags[] =
{
	1 << Input::Down,
	1 << Input::Left,
	1 << Input::Right,
	1 << Input::Up
};

/* Dir4 is always zero on these combinations */
static const int deadDirFlags[] =
{
	dirFlags[0] | dirFlags[3],
	dirFlags[1] | dirFlags[2]
};

static const Input::ButtonCode otherDirs[4][3] =
{
	{ Input::Left, Input::Right, Input::Up    }, /* Down  */
	{ Input::Down, Input::Up,    Input::Right }, /* Left  */
	{ Input::Down, Input::Up,    Input::Left  }, /* Right */
	{ Input::Left, Input::Right, Input::Up    }  /* Up    */
};

struct InputPrivate
{
	std::vector<KbBinding> kbStatBindings;
	std::vector<KbBinding> kbBindings;
	std::vector<JsAxisBinding> jsABindings;
	std::vector<JsHatBinding> jsHBindings;
	std::vector<JsButtonBinding> jsBBindings;
	std::vector<MsBinding> msBindings;

	/* Collective binding array */
	std::vector<Binding*> bindings;

	ButtonState stateArray[BUTTON_CODE_COUNT*2];

	ButtonState *states;
	ButtonState *statesOld;

	Input::ButtonCode repeating;
	unsigned int repeatCount;

	struct
	{
		int active;
		Input::ButtonCode previous;
	} dir4Data;

	struct
	{
		int active;
	} dir8Data;


	InputPrivate(const RGSSThreadData &rtData)
	{
		initStaticKbBindings();
		initMsBindings();

		/* Main thread should have these posted by now */
		checkBindingChange(rtData);

		/* WEB PORT: in the single-threaded emscripten build the bindingUpdateMsg
		 * is never delivered, so checkBindingChange returns without ever calling
		 * applyBindingDesc — leaving `bindings` empty and ALL keyboard input dead.
		 * Force-build the binding list from the (now game-control-carrying) static
		 * bindings so input works regardless of the message. */
		if (bindings.empty())
			applyBindingDesc(BDescVec());

		states    = stateArray;
		statesOld = stateArray + BUTTON_CODE_COUNT;

		/* Clear buffers */
		clearBuffer();
		swapBuffers();
		clearBuffer();

		repeating = Input::None;
		repeatCount = 0;

		dir4Data.active = 0;
		dir4Data.previous = Input::None;

		dir8Data.active = 0;
	}

	inline ButtonState &getStateCheck(int code)
	{
		int index;

		if (code < 0 || (size_t) code > mapToIndexN-1)
			index = 0;
		else
			index = mapToIndex[code];

		return states[index];
	}

	inline ButtonState &getState(Input::ButtonCode code)
	{
		return states[mapToIndex[code]];
	}

	inline ButtonState &getOldState(Input::ButtonCode code)
	{
		return statesOld[mapToIndex[code]];
	}

	void swapBuffers()
	{
		ButtonState *tmp = states;
		states = statesOld;
		statesOld = tmp;
	}

	void clearBuffer()
	{
		const size_t size = sizeof(ButtonState) * BUTTON_CODE_COUNT;
		memset(states, 0, size);
	}

	void checkBindingChange(const RGSSThreadData &rtData)
	{
		BDescVec d;

		if (!rtData.bindingUpdateMsg.poll(d))
			return;

		applyBindingDesc(d);
	}

	template<class B>
	void appendBindings(std::vector<B> &bind)
	{
		for (size_t i = 0; i < bind.size(); ++i)
			bindings.push_back(&bind[i]);
	}

	void applyBindingDesc(const BDescVec &d)
	{
		kbBindings.clear();
		jsABindings.clear();
		jsHBindings.clear();
		jsBBindings.clear();

		for (size_t i = 0; i < d.size(); ++i)
		{
			const BindingDesc &desc = d[i];
			const SourceDesc &src = desc.src;

			if (desc.target == Input::None)
				continue;

			switch (desc.src.type)
			{
			case Invalid :
				break;
			case Key :
			{
				KbBinding bind;
				bind.source = src.d.scan;
				bind.target = desc.target;
				kbBindings.push_back(bind);

				break;
			}
			case JAxis :
			{
				JsAxisBinding bind;
				bind.source = src.d.ja.axis;
				bind.dir = src.d.ja.dir;
				bind.target = desc.target;
				jsABindings.push_back(bind);

				break;
			}
			case JHat :
			{
				JsHatBinding bind;
				bind.source = src.d.jh.hat;
				bind.pos = src.d.jh.pos;
				bind.target = desc.target;
				jsHBindings.push_back(bind);

				break;
			}
			case JButton :
			{
				JsButtonBinding bind;
				bind.source = src.d.jb;
				bind.target = desc.target;
				jsBBindings.push_back(bind);

				break;
			}
			default :
				assert(!"unreachable");
			}
		}

		bindings.clear();

		appendBindings(kbStatBindings);
		appendBindings(msBindings);

		appendBindings(kbBindings);
		appendBindings(jsABindings);
		appendBindings(jsHBindings);
		appendBindings(jsBBindings);
	}

	void initStaticKbBindings()
	{
		kbStatBindings.clear();

		for (size_t i = 0; i < staticKbBindingsN; ++i)
			kbStatBindings.push_back(KbBinding(staticKbBindings[i]));
	}

	void initMsBindings()
	{
		msBindings.resize(3);

		size_t i = 0;
		msBindings[i++] = MsBinding(SDL_BUTTON_LEFT,   Input::MouseLeft);
		msBindings[i++] = MsBinding(SDL_BUTTON_MIDDLE, Input::MouseMiddle);
		msBindings[i++] = MsBinding(SDL_BUTTON_RIGHT,  Input::MouseRight);
	}

	void pollBindings(Input::ButtonCode &repeatCand)
	{
		for (size_t i = 0; i < bindings.size(); ++i)
			pollBindingPriv(*bindings[i], repeatCand);

		updateDir4();
		updateDir8();
	}

	void pollBindingPriv(const Binding &b,
	                     Input::ButtonCode &repeatCand)
	{	
		if (!b.sourceActive())
			return;

		if (b.target == Input::None)
			return;

		ButtonState &state = getState(b.target);
		ButtonState &oldState = getOldState(b.target);

		state.pressed = true;

		/* Must have been released before to trigger */
		if (!oldState.pressed)
			state.triggered = true;

		/* Unbound keys don't create/break repeat */
		if (repeatCand != Input::None)
			return;

		if (repeating != b.target &&
			!oldState.pressed)
		{
			if (b.sourceRepeatable())
				repeatCand = b.target;
			else
				/* Unrepeatable keys still break current repeat */
				repeating = Input::None;
		}
	}

	void updateDir4()
	{
		int dirFlag = 0;

		for (size_t i = 0; i < 4; ++i)
			dirFlag |= (getState(dirs[i]).pressed ? dirFlags[i] : 0);

		if (dirFlag == deadDirFlags[0] || dirFlag == deadDirFlags[1])
		{
			dir4Data.active = Input::None;
			return;
		}

		if (dir4Data.previous != Input::None)
		{
			/* Check if prev still pressed */
			if (getState(dir4Data.previous).pressed)
			{
				for (size_t i = 0; i < 3; ++i)
				{
					Input::ButtonCode other =
							otherDirs[(dir4Data.previous/2)-1][i];

					if (!getState(other).pressed)
						continue;

					dir4Data.active = other;
					return;
				}
			}
		}

		for (size_t i = 0; i < 4; ++i)
		{
			if (!getState(dirs[i]).pressed)
				continue;

			dir4Data.active = dirs[i];
			dir4Data.previous = dirs[i];
			return;
		}

		dir4Data.active   = Input::None;
		dir4Data.previous = Input::None;
	}

	void updateDir8()
	{
		static const int combos[4][4] =
		{
			{ 2, 1, 3, 0 },
			{ 1, 4, 0, 7 },
			{ 3, 0, 6, 9 },
			{ 0, 7, 9, 8 }
		};

		dir8Data.active = 0;

		for (size_t i = 0; i < 4; ++i)
		{
			Input::ButtonCode one = dirs[i];

			if (!getState(one).pressed)
				continue;

			for (int j = 0; j < 3; ++j)
			{
				Input::ButtonCode other = otherDirs[i][j];

				if (!getState(other).pressed)
					continue;

				dir8Data.active = combos[(one/2)-1][(other/2)-1];
				return;
			}

			dir8Data.active = one;
			return;
		}
	}
};


Input::Input(const RGSSThreadData &rtData)
{
	p = new InputPrivate(rtData);
}

void Input::update()
{
	shState->checkShutdown();
	p->checkBindingChange(shState->rtData());

	p->swapBuffers();
	p->clearBuffer();

	ButtonCode repeatCand = None;

	/* Poll all bindings */
	p->pollBindings(repeatCand);

	/* Check for new repeating key */
	if (repeatCand != None && repeatCand != p->repeating)
	{
		p->repeating = repeatCand;
		p->repeatCount = 0;
		p->getState(repeatCand).repeated = true;

		return;
	}

	/* Check if repeating key is still pressed */
	if (p->getState(p->repeating).pressed)
	{
		p->repeatCount++;

		bool repeated;
		if (rgssVer >= 2)
			repeated = p->repeatCount >= 23 && ((p->repeatCount+1) % 6) == 0;
		else
			repeated = p->repeatCount >= 15 && ((p->repeatCount+1) % 4) == 0;

		p->getState(p->repeating).repeated |= repeated;

		return;
	}

	p->repeating = None;
}

bool Input::isPressed(int button)
{
	return p->getStateCheck(button).pressed;
}

int Input::webBindingCount()
{
	return (int) p->bindings.size();
}

bool Input::isTriggered(int button)
{
	return p->getStateCheck(button).triggered;
}

bool Input::isRepeated(int button)
{
	return p->getStateCheck(button).repeated;
}

int Input::dir4Value()
{
	return p->dir4Data.active;
}

int Input::dir8Value()
{
	return p->dir8Data.active;
}

int Input::mouseX()
{
#ifdef MKXP_EMULATE_TOUCH_MOUSE
	return hasTouch() ? touchX(0) : -1;
#endif

	RGSSThreadData &rtData = shState->rtData();

	return (EventThread::mouseState.x - rtData.screenOffset.x) * rtData.sizeResoRatio.x;
}

int Input::mouseY()
{
#ifdef MKXP_EMULATE_TOUCH_MOUSE
	return hasTouch() ? touchY(0) : -1;
#endif

	RGSSThreadData &rtData = shState->rtData();

	return (EventThread::mouseState.y - rtData.screenOffset.y) * rtData.sizeResoRatio.y;
}

int Input::hasTouch()
{
	// TODO: multi-touch support
	return EventThread::touchState.fingers[0].down;
}

int Input::touchX(int finger)
{
	RGSSThreadData &rtData = shState->rtData();

	return (EventThread::touchState.fingers[finger].x - rtData.screenOffset.x) * rtData.sizeResoRatio.x;
}

int Input::touchY(int finger)
{
	RGSSThreadData &rtData = shState->rtData();

	return (EventThread::touchState.fingers[finger].y - rtData.screenOffset.y) * rtData.sizeResoRatio.y;
}

Input::~Input()
{
	delete p;
}
