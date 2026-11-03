/*
===========================================================================

PreyNX - controller input.

Buttons become K_JOY keys while playing, so every action can be rebound with
the game's own "bind" command. While a GUI is up the stick and the D-pad drive
the cursor, A clicks and B goes back. The touch screen clicks in GUIs. Clicking
a text field, or A while the console is down, opens the system keyboard.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#include "../../ui/DeviceContext.h"
#include "../../ui/Window.h"
#include "../../ui/UserInterfaceLocal.h"
#include "../../ui/EditWindow.h"

#undef BIT
#include <switch.h>

#include "switch_local.h"

static idCVar in_joystickDeadZone( "in_joystickDeadZone", "0.15", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "stick dead zone", 0.0f, 0.95f );
static idCVar in_joystickInvertLook( "in_joystickInvertLook", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "invert the right stick's vertical look" );
static idCVar in_menuCursorSpeed( "in_menuCursorSpeed", "14", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "menu cursor speed at full tilt, in pixels per frame", 1.0f, 60.0f );
static idCVar in_defaultBindsSet( "in_defaultBindsSet", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_NOCHEAT, "controller scheme the binds were last set up for (internal)" );

static const int SWITCH_CONTROL_SCHEME	= 1;

static const int SCREEN_WIDTH_PIXELS	= 1280;
static const int SCREEN_HEIGHT_PIXELS	= 720;
static const float GUI_WIDTH			= 640.0f;
static const float GUI_HEIGHT			= 480.0f;

typedef enum {
	MODE_GAME,
	MODE_MENU,
	MODE_CONSOLE
} inputMode_t;

// pseudo keys, handled here instead of being posted to the engine
static const int KEY_NONE			= 0;
static const int KEY_CONSOLE		= -1;	// toggles the console
static const int KEY_TEXT_ENTRY		= -2;	// opens the system keyboard for a console command

typedef struct {
	u64		button;
	int		gameKey;
	int		menuKey;
	int		consoleKey;
} buttonMap_t;

// labels for these keys are in framework/KeyInput.cpp
static const buttonMap_t s_buttonMap[] = {
	{ HidNpadButton_A,		K_JOY1,		K_MOUSE1,		KEY_TEXT_ENTRY },
	{ HidNpadButton_B,		K_JOY2,		K_ESCAPE,		KEY_CONSOLE },
	{ HidNpadButton_X,		K_JOY3,		KEY_NONE,		KEY_NONE },
	{ HidNpadButton_Y,		K_JOY4,		KEY_NONE,		KEY_NONE },
	{ HidNpadButton_L,		K_JOY5,		KEY_NONE,		K_PGUP },
	{ HidNpadButton_R,		K_JOY6,		KEY_NONE,		K_PGDN },
	{ HidNpadButton_ZL,		K_JOY7,		KEY_NONE,		KEY_NONE },
	{ HidNpadButton_ZR,		K_JOY8,		K_MOUSE1,		KEY_NONE },
	{ HidNpadButton_Plus,	K_ESCAPE,	K_ESCAPE,		KEY_NONE },
	{ HidNpadButton_Minus,	K_JOY10,	KEY_NONE,		KEY_CONSOLE },
	{ HidNpadButton_StickL,	K_JOY11,	KEY_NONE,		KEY_NONE },
	{ HidNpadButton_StickR,	K_JOY12,	KEY_NONE,		KEY_NONE },
	{ HidNpadButton_Up,		K_JOY13,	K_UPARROW,		K_UPARROW },
	{ HidNpadButton_Down,	K_JOY14,	K_DOWNARROW,	K_DOWNARROW },
	{ HidNpadButton_Left,	K_JOY15,	K_LEFTARROW,	KEY_NONE },
	{ HidNpadButton_Right,	K_JOY16,	K_RIGHTARROW,	KEY_NONE },
};
static const int NUM_BUTTONS = sizeof( s_buttonMap ) / sizeof( s_buttonMap[0] );

// Prey's actions on the Switch layout. Only keys the player left unbound are set.
typedef struct {
	int				key;
	const char *	command;
} defaultBind_t;

static const defaultBind_t s_defaultBinds[] = {
	{ K_JOY8,	"_attack" },		// ZR: fire, and use consoles
	{ K_JOY7,	"_attackalt" },		// ZL: alternate fire
	{ K_JOY1,	"_moveUp" },		// A: jump, wall walk up
	{ K_JOY2,	"_moveDown" },		// B: crouch
	{ K_JOY3,	"_impulse13" },		// X: reload
	{ K_JOY4,	"_impulse54" },		// Y: spirit walk
	{ K_JOY6,	"_impulse14" },		// R: next weapon
	{ K_JOY5,	"_impulse15" },		// L: previous weapon
	{ K_JOY13,	"_impulse16" },		// D-pad up: lighter
	{ K_JOY14,	"_impulse25" },		// D-pad down: crawler grenade
	{ K_JOY15,	"_impulse18" },		// D-pad left: center view
	{ K_JOY16,	"_zoom" },			// D-pad right: zoom
	{ K_JOY11,	"_speed" },			// left stick click: run
	{ K_JOY12,	"_zoom" },			// right stick click: zoom
	{ K_JOY10,	"_impulse19" },		// -: multiplayer scores
};

// a click on a stick is awkward to hold
static const char *s_toggleCvars[] = { "in_toggleRun", "in_toggleZoom" };

static const int QUEUE_SIZE = 256;
static const int QUEUE_MASK = QUEUE_SIZE - 1;

typedef struct {
	int		key;
	bool	down;
} keyEvent_t;

typedef struct {
	int		axis;
	int		value;
} axisEvent_t;

static keyEvent_t	s_keyQueue[QUEUE_SIZE];
static int			s_keyHead = 0;
static int			s_keyTail = 0;
static keyEvent_t	s_polledKeys[QUEUE_SIZE];
static int			s_polledKeyCount = 0;

static int			s_axes[MAX_JOYSTICK_AXIS];
static axisEvent_t	s_polledAxes[MAX_JOYSTICK_AXIS];
static int			s_polledAxisCount = 0;

static bool			s_inputReady = false;
static PadState		s_pad;
static u64			s_buttonsDown = 0;
static int			s_pressedAs[NUM_BUTTONS];	// so a release matches its press across mode changes
static inputMode_t	s_mode = MODE_GAME;
static float		s_cursorRestX = 0.0f;
static float		s_cursorRestY = 0.0f;
static bool			s_touchDown = false;
static bool			s_clickReleased = false;
static bool			s_consoleCombo = false;

/*
================
Key events
================
*/
static void Switch_PostKey( int key, bool down ) {
	if ( key == KEY_NONE ) {
		return;
	}
	Switch_QueueEvent( SE_KEY, key, down ? 1 : 0, 0, NULL );

	Sys_EnterCriticalSection( CRITICAL_SECTION_ONE );
	const int next = ( s_keyHead + 1 ) & QUEUE_MASK;
	if ( next == s_keyTail ) {
		s_keyTail = ( s_keyTail + 1 ) & QUEUE_MASK;
	}
	s_keyQueue[s_keyHead].key = key;
	s_keyQueue[s_keyHead].down = down;
	s_keyHead = next;
	Sys_LeaveCriticalSection( CRITICAL_SECTION_ONE );
}

static void Switch_ToggleConsole( void ) {
	Switch_QueueEvent( SE_KEY, Sys_GetConsoleKey( false ), 1, 0, NULL );
	Switch_QueueEvent( SE_KEY, Sys_GetConsoleKey( false ), 0, 0, NULL );
}

/*
================
System keyboard
================
*/
static bool Switch_ShowKeyboard( const char *header, char *text, size_t textSize ) {
	SwkbdConfig kbd;
	text[0] = '\0';
	if ( R_FAILED( swkbdCreate( &kbd, 0 ) ) ) {
		return false;
	}
	swkbdConfigMakePresetDefault( &kbd );
	swkbdConfigSetHeaderText( &kbd, header );
	const Result rc = swkbdShow( &kbd, text, textSize );
	swkbdClose( &kbd );
	return R_SUCCEEDED( rc ) && text[0] != '\0';
}

static void Switch_TypeText( const char *text ) {
	for ( const char *c = text; *c; c++ ) {
		const unsigned char ch = (unsigned char)*c;
		if ( ch >= 32 && ch < 127 ) {
			Switch_QueueEvent( SE_CHAR, ch, 0, 0, NULL );
		}
	}
}

static void Switch_ConsoleTextEntry( void ) {
	char text[256];
	if ( Switch_ShowKeyboard( "Prey console", text, sizeof( text ) ) ) {
		Switch_TypeText( text );
		Switch_PostKey( K_ENTER, true );
		Switch_PostKey( K_ENTER, false );
	}
}

// idEditWindow takes Backspace only as a character, not as a key
static void Switch_CheckGuiTextField( idUserInterface *gui ) {
	if ( !s_clickReleased ) {
		return;
	}
	s_clickReleased = false;
	if ( !gui || s_mode != MODE_MENU ) {
		return;
	}

	idWindow *desktop = static_cast<idUserInterfaceLocal *>( gui )->GetDesktop();
	idEditWindow *field = desktop ? dynamic_cast<idEditWindow *>( desktop->GetFocusedChild() ) : NULL;
	if ( !field || !field->Contains( gui->CursorX(), gui->CursorY() ) ) {
		return;
	}

	char text[128];
	if ( !Switch_ShowKeyboard( "Prey", text, sizeof( text ) ) ) {
		return;
	}
	Switch_PostKey( K_END, true );
	Switch_PostKey( K_END, false );
	idWinVar *current = field->GetWinVarByName( "text" );
	const int eraseChars = Max( 64, current ? idStr::Length( current->c_str() ) : 0 );
	for ( int i = 0; i < eraseChars; i++ ) {
		Switch_QueueEvent( SE_CHAR, K_BACKSPACE, 0, 0, NULL );
	}
	Switch_TypeText( text );
}

/*
================
Per-frame updates
================
*/
static int Switch_KeyForMode( const buttonMap_t &map ) {
	switch ( s_mode ) {
		case MODE_MENU:
			return map.menuKey;
		case MODE_CONSOLE:
			return map.consoleKey;
		default:
			return map.gameKey;
	}
}

static void Switch_ReleaseButtons( void ) {
	for ( int i = 0; i < NUM_BUTTONS; i++ ) {
		if ( s_pressedAs[i] != KEY_NONE ) {
			if ( s_pressedAs[i] > 0 ) {
				Switch_PostKey( s_pressedAs[i], false );
			}
			s_pressedAs[i] = KEY_NONE;
		}
	}
}

static void Switch_UpdateButtons( u64 held ) {
	// + and - together open the console from anywhere
	const u64 combo = HidNpadButton_Plus | HidNpadButton_Minus;
	if ( ( held & combo ) == combo ) {
		if ( !s_consoleCombo ) {
			s_consoleCombo = true;
			Switch_ReleaseButtons();
			Switch_ToggleConsole();
		}
		s_buttonsDown = held;
		return;
	}
	s_consoleCombo = false;

	const u64 changed = held ^ s_buttonsDown;
	s_buttonsDown = held;
	for ( int i = 0; i < NUM_BUTTONS; i++ ) {
		if ( !( changed & s_buttonMap[i].button ) ) {
			continue;
		}
		if ( held & s_buttonMap[i].button ) {
			const int key = Switch_KeyForMode( s_buttonMap[i] );
			s_pressedAs[i] = key;
			if ( key == KEY_CONSOLE ) {
				Switch_ToggleConsole();
			} else if ( key == KEY_TEXT_ENTRY ) {
				Switch_ConsoleTextEntry();
			} else {
				Switch_PostKey( key, true );
			}
		} else {
			if ( s_pressedAs[i] > 0 ) {
				Switch_PostKey( s_pressedAs[i], false );
				if ( s_pressedAs[i] == K_MOUSE1 ) {
					s_clickReleased = true;
				}
			}
			s_pressedAs[i] = KEY_NONE;
		}
	}
}

static int Switch_NormalizeStick( s32 value, float deadZone ) {
	float v = value / (float)JOYSTICK_MAX;
	const float mag = idMath::Fabs( v );
	if ( mag <= deadZone ) {
		return 0;
	}
	v = ( v > 0.0f ? 1.0f : -1.0f ) * ( mag - deadZone ) / ( 1.0f - deadZone );
	return idMath::ClampInt( -127, 127, (int)( v * 127.0f ) );
}

static void Switch_ClearAxes( void ) {
	Sys_EnterCriticalSection( CRITICAL_SECTION_ONE );
	memset( s_axes, 0, sizeof( s_axes ) );
	Sys_LeaveCriticalSection( CRITICAL_SECTION_ONE );
}

static void Switch_UpdateGameAxes( const HidAnalogStickState &left, const HidAnalogStickState &right ) {
	const float deadZone = in_joystickDeadZone.GetFloat();
	int lookY = -Switch_NormalizeStick( right.y, deadZone );
	if ( in_joystickInvertLook.GetBool() ) {
		lookY = -lookY;
	}

	Sys_EnterCriticalSection( CRITICAL_SECTION_ONE );
	s_axes[AXIS_SIDE] = Switch_NormalizeStick( right.x, deadZone );
	s_axes[AXIS_FORWARD] = lookY;
	s_axes[AXIS_UP] = 0;
	s_axes[AXIS_ROLL] = 127;	// dedicated look axes are present (UsercmdGen.cpp)
	s_axes[AXIS_YAW] = Switch_NormalizeStick( left.x, deadZone );
	s_axes[AXIS_PITCH] = Switch_NormalizeStick( left.y, deadZone );
	Sys_LeaveCriticalSection( CRITICAL_SECTION_ONE );
}

static void Switch_UpdateMenuCursor( const HidAnalogStickState &left ) {
	const float deadZone = in_joystickDeadZone.GetFloat();
	const float x = Switch_NormalizeStick( left.x, deadZone ) / 127.0f;
	const float y = -Switch_NormalizeStick( left.y, deadZone ) / 127.0f;
	if ( x == 0.0f && y == 0.0f ) {
		s_cursorRestX = s_cursorRestY = 0.0f;
		return;
	}
	// fine control near the center, fast at full tilt
	const float speed = in_menuCursorSpeed.GetFloat();
	s_cursorRestX += x * idMath::Fabs( x ) * speed;
	s_cursorRestY += y * idMath::Fabs( y ) * speed;
	const int dx = (int)s_cursorRestX;
	const int dy = (int)s_cursorRestY;
	s_cursorRestX -= dx;
	s_cursorRestY -= dy;
	if ( dx || dy ) {
		Switch_QueueEvent( SE_MOUSE, dx, dy, 0, NULL );
	}
}

static void Switch_UpdateTouch( idUserInterface *gui ) {
	HidTouchScreenState touch = {};
	const bool touching = hidGetTouchScreenStates( &touch, 1 ) && touch.count > 0;

	if ( touching && gui ) {
		const float x = idMath::ClampFloat( 0.0f, GUI_WIDTH, touch.touches[0].x * GUI_WIDTH / SCREEN_WIDTH_PIXELS );
		const float y = idMath::ClampFloat( 0.0f, GUI_HEIGHT, touch.touches[0].y * GUI_HEIGHT / SCREEN_HEIGHT_PIXELS );
		gui->SetCursor( x, y );
	}
	// a click starts only inside a GUI; the release always comes through
	if ( touching != s_touchDown && ( !touching || gui ) ) {
		Switch_PostKey( K_MOUSE1, touching );
		s_touchDown = touching;
		if ( !touching ) {
			s_clickReleased = true;
		}
	}
}

void Switch_PollInput( void ) {
	if ( !s_inputReady ) {
		return;
	}

	padUpdate( &s_pad );

	idUserInterface *gui = session ? session->GetActiveGUI() : NULL;
	inputMode_t mode = MODE_GAME;
	if ( console && console->Active() ) {
		mode = MODE_CONSOLE;
	} else if ( gui ) {
		mode = MODE_MENU;
	}
	if ( mode != s_mode ) {
		Switch_ReleaseButtons();
		Switch_ClearAxes();
		s_cursorRestX = s_cursorRestY = 0.0f;
		s_mode = mode;
		// a button held across the change does not fire again in the new mode
		s_buttonsDown = padGetButtons( &s_pad );
	}

	Switch_CheckGuiTextField( gui );
	Switch_UpdateButtons( padGetButtons( &s_pad ) );

	const HidAnalogStickState left = padGetStickPos( &s_pad, 0 );
	const HidAnalogStickState right = padGetStickPos( &s_pad, 1 );
	if ( s_mode == MODE_GAME ) {
		Switch_UpdateGameAxes( left, right );
	} else {
		Switch_ClearAxes();
		if ( s_mode == MODE_MENU ) {
			Switch_UpdateMenuCursor( left );
		}
	}

	Switch_UpdateTouch( s_mode == MODE_MENU ? gui : NULL );
}

void Switch_ApplyDefaultBinds( void ) {
	if ( in_defaultBindsSet.GetInteger() >= SWITCH_CONTROL_SCHEME ) {
		return;
	}
	for ( size_t i = 0; i < sizeof( s_defaultBinds ) / sizeof( s_defaultBinds[0] ); i++ ) {
		const char *current = idKeyInput::GetBinding( s_defaultBinds[i].key );
		if ( !current || !current[0] ) {
			idKeyInput::SetBinding( s_defaultBinds[i].key, s_defaultBinds[i].command );
		}
	}
	for ( size_t i = 0; i < sizeof( s_toggleCvars ) / sizeof( s_toggleCvars[0] ); i++ ) {
		cvarSystem->SetCVarString( s_toggleCvars[i], "1" );
	}
	in_defaultBindsSet.SetInteger( SWITCH_CONTROL_SCHEME );
}

/*
================
System interface
================
*/
void Sys_InitInput( void ) {
	padConfigureInput( 1, HidNpadStyleSet_NpadStandard );
	padInitializeDefault( &s_pad );
	hidInitializeTouchScreen();
	memset( s_pressedAs, 0, sizeof( s_pressedAs ) );
	memset( s_axes, 0, sizeof( s_axes ) );
	s_buttonsDown = 0;
	s_mode = MODE_GAME;
	s_touchDown = false;
	s_inputReady = true;
}

void Sys_ShutdownInput( void ) {
	s_inputReady = false;
}

void Sys_InitScanTable( void ) {
}

unsigned char Sys_GetConsoleKey( bool shifted ) {
	return shifted ? '~' : '`';
}

unsigned char Sys_MapCharForKey( int key ) {
	return (unsigned char)key;
}

int Sys_PollKeyboardInputEvents( void ) {
	Sys_EnterCriticalSection( CRITICAL_SECTION_ONE );
	s_polledKeyCount = 0;
	while ( s_keyTail != s_keyHead ) {
		s_polledKeys[s_polledKeyCount++] = s_keyQueue[s_keyTail];
		s_keyTail = ( s_keyTail + 1 ) & QUEUE_MASK;
	}
	Sys_LeaveCriticalSection( CRITICAL_SECTION_ONE );
	return s_polledKeyCount;
}

int Sys_ReturnKeyboardInputEvent( const int n, int &ch, bool &state ) {
	if ( n < 0 || n >= s_polledKeyCount ) {
		ch = 0;
		state = false;
		return 0;
	}
	ch = s_polledKeys[n].key;
	state = s_polledKeys[n].down;
	return ch;
}

void Sys_EndKeyboardInputEvents( void ) {
}

int Sys_PollMouseInputEvents( void ) {
	return 0;
}

int Sys_ReturnMouseInputEvent( const int n, int &action, int &value ) {
	action = 0;
	value = 0;
	return 0;
}

void Sys_EndMouseInputEvents( void ) {
}

int Sys_PollJoystickInputEvents( void ) {
	Sys_EnterCriticalSection( CRITICAL_SECTION_ONE );
	s_polledAxisCount = 0;
	for ( int axis = 0; axis < MAX_JOYSTICK_AXIS; axis++ ) {
		s_polledAxes[s_polledAxisCount].axis = axis;
		s_polledAxes[s_polledAxisCount].value = s_axes[axis];
		s_polledAxisCount++;
	}
	Sys_LeaveCriticalSection( CRITICAL_SECTION_ONE );
	return s_polledAxisCount;
}

int Sys_ReturnJoystickInputEvent( const int n, int &axis, int &value ) {
	if ( n < 0 || n >= s_polledAxisCount ) {
		axis = 0;
		value = 0;
		return 0;
	}
	axis = s_polledAxes[n].axis;
	value = s_polledAxes[n].value;
	return 1;
}

void Sys_EndJoystickInputEvents( void ) {
}
