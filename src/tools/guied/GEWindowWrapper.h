/*
===========================================================================

PreyNX - the GUI editor is not part of this build. The window code only keeps
this class's data for it, while editing is on, which it never is here.

===========================================================================
*/

#ifndef __GEWINDOWWRAPPER_H__
#define __GEWINDOWWRAPPER_H__

class idWindow;

class rvGEWindowWrapper {
public:
	enum EWindowType {
		WT_UNKNOWN,
		WT_NORMAL,
		WT_EDIT,
		WT_HTML,
		WT_CHOICE,
		WT_SLIDER,
		WT_BIND,
		WT_LIST,
		WT_RENDER,
		WT_TRANSITION
	};

							rvGEWindowWrapper( idWindow *window, EWindowType type ) {}

	static rvGEWindowWrapper *GetWrapper( idWindow *window ) { return NULL; }

	idDict &				GetStateDict( void ) { return dict; }
	idDict &				GetVariableDict( void ) { return dict; }
	idDict &				GetScriptDict( void ) { return dict; }
	void					SetStateKey( const char *key, const char *value, bool update = true ) {}
	void					Finish( void ) {}

private:
	idDict					dict;
};

#endif /* !__GEWINDOWWRAPPER_H__ */
