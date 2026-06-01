#include "../Core.h"
#if defined CC_BUILD_PLAN9

#include "../_WindowBase.h"
#include "../String.h"
#include "../Funcs.h"
#include "../Bitmap.h"
#include "../Options.h"
#include "../Errors.h"

#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <keyboard.h>

/* Native 9front window backend.
   - Rendering: a software framebuffer (Bitmap) is converted to a Plan 9
     RGB24 Image and blitted to the rio window via draw()/flushimage().
   - Input: keyboard via /dev/kbd (k/K/c messages listing all currently
     pressed runes), mouse via the libdraw event(2) interface. We diff the
     pressed-rune set frame-to-frame to synthesise key press/release events,
     which is what ClassiCube needs for held-key movement. */

#define DEV_KBD_KEY 8   /* custom estart() key for /dev/kbd raw stream */

static int kbdfd = -1;
static Image* fbImg;          /* offscreen RGB24 image matching framebuffer */
static int fbWidth, fbHeight;
static int mouseX, mouseY;    /* last known cursor position, window-relative */
static cc_uint8* fbLine;      /* scratch BGR row buffer for blitting */

/* Tracks which runes were pressed in the previous /dev/kbd frame, so we can
   detect transitions. Stores up to 32 simultaneously-held runes. */
static Rune prevKeys[32];
static int  prevKeyCount;

/*########################################################################################################################*
*-----------------------------------------------------Key mapping---------------------------------------------------------*
*#########################################################################################################################*/
static int MapKey(Rune r) {
	if (r >= 'a' && r <= 'z') return 'A' + (r - 'a');
	if (r >= 'A' && r <= 'Z') return r;
	if (r >= '0' && r <= '9') return r;

	switch (r) {
	case Kup:        return CCKEY_UP;
	case Kdown:      return CCKEY_DOWN;
	case Kleft:      return CCKEY_LEFT;
	case Kright:     return CCKEY_RIGHT;

	case Kesc:       return CCKEY_ESCAPE;
	case '\n':       return CCKEY_ENTER;
	case '\t':       return CCKEY_TAB;
	case Kbs:        return CCKEY_BACKSPACE;
	case Kdel:       return CCKEY_DELETE;
	case Kins:       return CCKEY_INSERT;
	case Khome:      return CCKEY_HOME;
	case Kend:       return CCKEY_END;
	case Kpgup:      return CCKEY_PAGEUP;
	case Kpgdown:    return CCKEY_PAGEDOWN;
	case ' ':        return CCKEY_SPACE;

	case Kshift:     return CCKEY_LSHIFT;
	case Kctl:       return CCKEY_LCTRL;
	case Kalt:       return CCKEY_LALT;

	case KF|1:       return CCKEY_F1;
	case KF|2:       return CCKEY_F2;
	case KF|3:       return CCKEY_F3;
	case KF|4:       return CCKEY_F4;
	case KF|5:       return CCKEY_F5;
	case KF|6:       return CCKEY_F6;
	case KF|7:       return CCKEY_F7;
	case KF|8:       return CCKEY_F8;
	case KF|9:       return CCKEY_F9;
	case KF|10:      return CCKEY_F10;
	case KF|11:      return CCKEY_F11;
	case KF|12:      return CCKEY_F12;

	case '-':        return CCKEY_MINUS;
	case '=':        return CCKEY_EQUALS;
	case '[':        return CCKEY_LBRACKET;
	case ']':        return CCKEY_RBRACKET;
	case '\\':       return CCKEY_BACKSLASH;
	case ';':        return CCKEY_SEMICOLON;
	case '\'':       return CCKEY_QUOTE;
	case ',':        return CCKEY_COMMA;
	case '.':        return CCKEY_PERIOD;
	case '/':        return CCKEY_SLASH;
	case '`':        return CCKEY_TILDE;
	}
	return INPUT_NONE;
}

/*########################################################################################################################*
*-----------------------------------------------------Public implementation-----------------------------------------------*
*#########################################################################################################################*/
void Window_PreInit(void) {
	DisplayInfo.CursorVisible = true;
}

void Window_Init(void) {
	if (initdraw(nil, nil, "ClassiCube") < 0) {
		Process_Abort("Failed to initialise libdraw");
	}

	DisplayInfo.Width  = Dx(screen->r);
	DisplayInfo.Height = Dy(screen->r);
	DisplayInfo.Depth  = screen->depth;
	DisplayInfo.ScaleX = 1.0f;
	DisplayInfo.ScaleY = 1.0f;

	Input.Sources = INPUT_SOURCE_NORMAL;

	/* Open the device-independent keyboard for raw press/release tracking. */
	kbdfd = open("/dev/kbd", OREAD);
	einit(Emouse);
	if (kbdfd >= 0) estart(DEV_KBD_KEY, kbdfd, 128);
}

void Window_Free(void) {
	if (fbImg)  { freeimage(fbImg); fbImg = nil; }
	if (kbdfd >= 0) { close(kbdfd); kbdfd = -1; }
}

static void RefreshWindowBounds(void) {
	Window_Main.Width  = Dx(screen->r);
	Window_Main.Height = Dy(screen->r);
}

static void DoCreateWindow(int width, int height) {
	if (Window_Main.Exists) return;

	Window_Main.Exists   = true;
	Window_Main.Focused  = true;
	Window_Main.UIScaleX = DEFAULT_UI_SCALE_X;
	Window_Main.UIScaleY = DEFAULT_UI_SCALE_Y;

	RefreshWindowBounds();
	Event_RaiseVoid(&WindowEvents.Resized);
}

void Window_Create2D(int width, int height) { DoCreateWindow(width, height); }
void Window_Create3D(int width, int height) { DoCreateWindow(width, height); }

void Window_Destroy(void) { }

void Window_SetTitle(const cc_string* title) {
	char str[256];
	int fd;
	String_EncodeUtf8(str, title);

	/* rio exposes the window label via /dev/label */
	fd = open("/dev/label", OWRITE);
	if (fd >= 0) {
		write(fd, str, String_Length(str));
		close(fd);
	}
}

void Clipboard_GetText(cc_string* value) {
	int fd = open("/dev/snarf", OREAD);
	char buf[1024];
	long n;
	if (fd < 0) return;

	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		String_AppendUtf8(value, buf, n);
	}
	close(fd);
}

void Clipboard_SetText(const cc_string* value) {
	char str[2048];
	int fd, len;
	len = String_EncodeUtf8(str, value);

	fd = open("/dev/snarf", OWRITE | OTRUNC);
	if (fd < 0) return;
	write(fd, str, len);
	close(fd);
}

int Window_GetWindowState(void) {
	return WINDOW_STATE_NORMAL;
}

cc_result Window_EnterFullscreen(void) { return ERR_NOT_SUPPORTED; }
cc_result Window_ExitFullscreen(void)  { return ERR_NOT_SUPPORTED; }
int  Window_IsObscured(void)           { return 0; }
void Window_Show(void)                 { }

void Window_SetSize(int width, int height) {
	/* rio owns the window geometry; we render into whatever it gives us. */
	RefreshWindowBounds();
}

void Window_RequestClose(void) {
	Window_Main.Exists = false;
	Event_RaiseVoid(&WindowEvents.Closing);
}

/* Updates the pressed-key set from a /dev/kbd message and raises CC events. */
static void HandleKeysFrame(Rune* keys, int count) {
	int i, j;
	cc_bool found;

	/* Presses: runes present now that weren't before. */
	for (i = 0; i < count; i++) {
		found = false;
		for (j = 0; j < prevKeyCount; j++) {
			if (prevKeys[j] == keys[i]) { found = true; break; }
		}
		if (!found) {
			int key = MapKey(keys[i]);
			if (key) Input_SetPressed(key);
			if (keys[i] >= ' ' && keys[i] != Kdel && keys[i] < Spec)
				Event_RaiseInt(&InputEvents.Press, keys[i]);
		}
	}

	/* Releases: runes present before that aren't now. */
	for (j = 0; j < prevKeyCount; j++) {
		found = false;
		for (i = 0; i < count; i++) {
			if (keys[i] == prevKeys[j]) { found = true; break; }
		}
		if (!found) {
			int key = MapKey(prevKeys[j]);
			if (key) Input_SetReleased(key);
		}
	}

	if (count > 32) count = 32;
	for (i = 0; i < count; i++) prevKeys[i] = keys[i];
	prevKeyCount = count;
}

/* Parses a raw /dev/kbd message: a leading 'k'/'K'/'c' byte then a
   NUL-terminated UTF-8 string of all currently pressed keys. */
static void ProcessKbdMessage(char* buf, int n) {
	Rune keys[32];
	int count = 0;
	char* p;
	Rune r;

	if (n < 1) return;
	if (buf[0] != 'k' && buf[0] != 'K') return; /* ignore 'c' (compose) */
	buf[n] = '\0';

	p = buf + 1;
	while (*p && count < 32) {
		p += chartorune(&r, p);
		keys[count++] = r;
	}
	HandleKeysFrame(keys, count);
}

/* Required by the libdraw event(2) mechanism: called when our rio window
   is resized. We must re-establish the window connection via getwindow,
   then notify ClassiCube so it can reallocate its framebuffer. */
void eresized(int new) {
	if (new && getwindow(display, Refnone) < 0)
		Process_Abort("Failed to reattach to resized window");

	RefreshWindowBounds();
	Event_RaiseVoid(&WindowEvents.Resized);
}

void Window_ProcessEvents(float delta) {
	Event ev;
	ulong key;

	if (!Window_Main.Exists) return;

	while (ecanread(Emouse | DEV_KBD_KEY)) {
		key = eread(Emouse | DEV_KBD_KEY, &ev);

		if (key == Emouse) {
			Mouse m = ev.mouse;
			mouseX = m.xy.x - screen->r.min.x;
			mouseY = m.xy.y - screen->r.min.y;
			Pointer_SetPosition(0, mouseX, mouseY);
			Input_SetNonRepeatable(CCMOUSE_L, m.buttons & 1);
			Input_SetNonRepeatable(CCMOUSE_M, m.buttons & 2);
			Input_SetNonRepeatable(CCMOUSE_R, m.buttons & 4);
		} else if (key == DEV_KBD_KEY) {
			ProcessKbdMessage((char*)ev.data, ev.n);
		}
	}
}

void Gamepads_PreInit(void) { }
void Gamepads_Init(void)    { }
void Gamepads_Process(float delta) { }

static void Cursor_GetRawPos(int* x, int* y) {
	*x = mouseX;
	*y = mouseY;
}

void Cursor_SetPosition(int x, int y) {
	emoveto(Pt(screen->r.min.x + x, screen->r.min.y + y));
}

static void Cursor_DoSetVisible(cc_bool visible) { }

static void ShowDialogCore(const char* title, const char* msg) {
	Platform_LogConst(title);
	Platform_LogConst(msg);
}

cc_result Window_OpenFileDialog(const struct OpenFileDialogArgs* args) {
	return ERR_NOT_SUPPORTED;
}

cc_result Window_SaveFileDialog(const struct SaveFileDialogArgs* args) {
	return ERR_NOT_SUPPORTED;
}

/*########################################################################################################################*
*-------------------------------------------------------Framebuffer-------------------------------------------------------*
*#########################################################################################################################*/
void Window_AllocFramebuffer(struct Bitmap* bmp, int width, int height) {
	bmp->scan0  = (BitmapCol*)Mem_Alloc(width * height, BITMAPCOLOR_SIZE, "window pixels");
	bmp->width  = width;
	bmp->height = height;

	if (fbImg)  { freeimage(fbImg); fbImg = nil; }
	if (fbLine) { Mem_Free(fbLine); fbLine = nil; }

	fbImg    = allocimage(display, Rect(0, 0, width, height), RGB24, 0, DNofill);
	fbLine   = (cc_uint8*)Mem_Alloc(width * height, 3, "plan9 BGR buffer");
	fbWidth  = width;
	fbHeight = height;
}

void Window_DrawFramebuffer(Rect2D r, struct Bitmap* bmp) {
	/* Plan 9 RGB24 stores pixels as B,G,R bytes in memory. ClassiCube's
	   BitmapCol is R,G,B,A in memory, so we repack the whole image to BGR
	   then upload it in a single loadimage call. */
	int x, y, w, h, i;
	BitmapCol* row;

	if (!fbImg || !fbLine || !bmp || !bmp->scan0) return;
	w = bmp->width;
	h = bmp->height;

	i = 0;
	for (y = 0; y < h; y++) {
		row = Bitmap_GetRow(bmp, y);
		for (x = 0; x < w; x++) {
			BitmapCol col = row[x];
			fbLine[i++] = BitmapCol_B(col);
			fbLine[i++] = BitmapCol_G(col);
			fbLine[i++] = BitmapCol_R(col);
		}
	}

	loadimage(fbImg, Rect(0, 0, w, h), fbLine, w * h * 3);
	draw(screen, screen->r, fbImg, nil, ZP);
	flushimage(display, 1);
}

void Window_FreeFramebuffer(struct Bitmap* bmp) {
	Mem_Free(bmp->scan0);
	if (fbImg)  { freeimage(fbImg); fbImg = nil; }
	if (fbLine) { Mem_Free(fbLine); fbLine = nil; }
}

void OnscreenKeyboard_Open(struct OpenKeyboardArgs* args) { }
void OnscreenKeyboard_SetText(const cc_string* text) { }
void OnscreenKeyboard_Close(void) { }

void Window_EnableRawMouse(void)  { DefaultEnableRawMouse();  }
void Window_UpdateRawMouse(void)  { DefaultUpdateRawMouse();  }
void Window_DisableRawMouse(void) { DefaultDisableRawMouse(); }
#endif
