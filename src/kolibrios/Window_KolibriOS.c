#include "../_WindowBase.h"
#include "../String.h"
#include "../Funcs.h"
#include "../Bitmap.h"
#include "../Options.h"
#include "../Errors.h"
#include <sys/ksys.h>

static int win_pos_x = 100;
static int win_pos_y = 100;
static cc_uint8* buffer24_static = NULL;
static int buffer24_size = 0;

/*########################################################################################################################*
*--------------------------------------------------Public implementation--------------------------------------------------*
*#########################################################################################################################*/
void Window_PreInit(void) {
    DisplayInfo.CursorVisible = true;
}

void Window_Init(void) {
    ksys_pos_t screen = _ksys_screen_size();

    DisplayInfo.Width  = screen.x;
    DisplayInfo.Height = screen.y;
    DisplayInfo.Depth  = 24;
    DisplayInfo.ScaleX = 1.0f;
    DisplayInfo.ScaleY = 1.0f;

    Input.Sources = INPUT_SOURCE_NORMAL;
}

void Window_Free(void) {
    //
    if (buffer24_static) {
        Mem_Free(buffer24_static);
        buffer24_static = NULL;
    }
}

static void DoCreateWindow(int width, int height) {
    if (Window_Main.Exists) return;

    //
    ksys_pos_t screen = _ksys_screen_size();
    win_pos_x = screen.x/2 - width/2;
    win_pos_y = screen.y/2 - height/2;

    _ksys_set_event_mask(0x27);
    _ksys_create_window(win_pos_x, win_pos_y, width, height, "ClassiCube", 0x000000, 0x34);

    Window_Main.Width    = width;
    Window_Main.Height   = height;
    Window_Main.Focused  = true;
    Window_Main.Exists   = true;
    Window_Main.UIScaleX = DEFAULT_UI_SCALE_X;
    Window_Main.UIScaleY = DEFAULT_UI_SCALE_Y;

    //
    Window_SetSize(width, height);
}

void Window_Create2D(int width, int height) { DoCreateWindow(width, height); }
void Window_Create3D(int width, int height) { DoCreateWindow(width, height); }

void Window_Destroy(void) { }

void Window_SetTitle(const cc_string* title) {
    char str[256];
    String_EncodeUtf8(str, title);
    _ksys_set_window_title(str);
}

void Clipboard_GetText(cc_string* value) { }
void Clipboard_SetText(const cc_string* value) { }

int Window_GetWindowState(void) {
    return WINDOW_STATE_NORMAL;
}

cc_result Window_EnterFullscreen(void) { return ERR_NOT_SUPPORTED; }
cc_result Window_ExitFullscreen(void) { return ERR_NOT_SUPPORTED; }
int Window_IsObscured(void) { return 0; }
void Window_Show(void) { }

void Window_SetSize(int width, int height) {
    //
    _ksys_change_window(win_pos_x, win_pos_y, width, height);
    Window_Main.Width  = width;
    Window_Main.Height = height;

    //
    if (buffer24_static) {
        Mem_Free(buffer24_static);
        buffer24_static = NULL;
    }

    int pixel_count = width * height;
    buffer24_size = pixel_count * 3;
    buffer24_static = (cc_uint8*)Mem_Alloc(buffer24_size, 1, "static 24bpp buffer");

    if (!buffer24_static) {
        //
        Platform_LogConst("ERROR: Failed to allocate static 24bpp buffer!");
        Window_RequestClose();
    }
}

void Window_RequestClose(void) {
    Window_Main.Exists = false;
    Event_RaiseVoid(&WindowEvents.Closing);
}

static int MapKey(int scancode) {
    if (scancode >= KSYS_SCANCODE_A && scancode <= KSYS_SCANCODE_Z) {
        return 'A' + (scancode - KSYS_SCANCODE_A);
    }
    if (scancode >= KSYS_SCANCODE_0 && scancode <= KSYS_SCANCODE_9) {
        return '0' + (scancode - KSYS_SCANCODE_0);
    }

    switch(scancode) {
        case KSYS_SCANCODE_SPACE: return CCKEY_SPACE;
        case KSYS_SCANCODE_ENTER: return CCKEY_ENTER;
        case KSYS_SCANCODE_TAB: return CCKEY_TAB;
        case KSYS_SCANCODE_BACKSPACE: return CCKEY_BACKSPACE;
        case KSYS_SCANCODE_ESC: return CCKEY_ESCAPE;
        case 72: return CCKEY_UP;
        case 80: return CCKEY_DOWN;
        case 75: return CCKEY_LEFT;
        case 77: return CCKEY_RIGHT;
        case KSYS_SCANCODE_LSHIFT: return CCKEY_LSHIFT;
        case KSYS_SCANCODE_LCTRL: return CCKEY_LCTRL;
        case KSYS_SCANCODE_F1: return CCKEY_F1;
        case KSYS_SCANCODE_F2: return CCKEY_F2;
        case KSYS_SCANCODE_F3: return CCKEY_F3;
        case KSYS_SCANCODE_F4: return CCKEY_F4;
        case KSYS_SCANCODE_F5: return CCKEY_F5;
        case KSYS_SCANCODE_F6: return CCKEY_F6;
        case KSYS_SCANCODE_F7: return CCKEY_F7;
        case KSYS_SCANCODE_F8: return CCKEY_F8;
        case KSYS_SCANCODE_F9: return CCKEY_F9;
        case KSYS_SCANCODE_F10: return CCKEY_F10;
        case KSYS_SCANCODE_F11: return CCKEY_F11;
        case KSYS_SCANCODE_F12: return CCKEY_F12;

        //
        case 71: return CCKEY_HOME;
        case 79: return CCKEY_END;
    }
    return 0;
}

void Window_ProcessEvents(float delta) {
    if (!Window_Main.Exists) return;

    uint32_t event = _ksys_check_event();

    switch(event) {
        case KSYS_EVENT_NONE:
            break;

        case KSYS_EVENT_REDRAW: {
            _ksys_start_draw();
            //
            _ksys_create_window(win_pos_x, win_pos_y,
                                 Window_Main.Width, Window_Main.Height,
                                 "ClassiCube", 0x000000, 0x34);
            Event_RaiseVoid(&WindowEvents.RedrawNeeded);
            _ksys_end_draw();
            break;
        }

        case KSYS_EVENT_KEY: {
            ksys_oskey_t key = _ksys_get_key();
            int cckey = MapKey(key.code);

            if (cckey) Input_Set(cckey, true);

            if (key.code >= 32 && key.code < 127) {
                Event_RaiseInt(&InputEvents.Press, (cc_unichar)key.code);
            }
            break;
        }

        case KSYS_EVENT_BUTTON:
            Window_RequestClose();
            break;

        case KSYS_EVENT_MOUSE: {
            ksys_pos_t pos = _ksys_get_mouse_pos(KSYS_MOUSE_WINDOW_POS);
            uint32_t buttons = _ksys_get_mouse_buttons();

            Pointer_SetPosition(0, pos.x, pos.y);

            Input_SetNonRepeatable(CCMOUSE_L, buttons & KSYS_MOUSE_LBUTTON_PRESSED);
            Input_SetNonRepeatable(CCMOUSE_R, buttons & KSYS_MOUSE_RBUTTON_PRESSED);
            Input_SetNonRepeatable(CCMOUSE_M, buttons & KSYS_MOUSE_MBUTTON_PRESSED);
            break;
        }
    }
}

void Gamepads_PreInit(void) { }
void Gamepads_Init(void) { }
void Gamepads_Process(float delta) { }

static void Cursor_GetRawPos(int* x, int* y) {
    ksys_pos_t pos = _ksys_get_mouse_pos(KSYS_MOUSE_WINDOW_POS);
    *x = pos.x;
    *y = pos.y;
}

void Cursor_SetPosition(int x, int y) {
    _ksys_set_mouse_pos(x, y);
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

void Window_AllocFramebuffer(struct Bitmap* bmp, int width, int height) {
    //
    bmp->scan0  = (BitmapCol*)Mem_Alloc(width * height, BITMAPCOLOR_SIZE, "window pixels");
    bmp->width  = width;
    bmp->height = height;

    if (!bmp->scan0) {
        //
        Platform_LogConst("ERROR: Failed to allocate 32bpp SoftGPU buffer!");
        Window_RequestClose();
    }
}

void Window_DrawFramebuffer(Rect2D r, struct Bitmap* bmp) {
    if (!bmp || !bmp->scan0 || !buffer24_static) return;

    int width  = bmp->width;
    int height = bmp->height;
    cc_uint8* buffer24 = buffer24_static; //

    int buf_idx = 0;
    for (int y = 0; y < height; y++) {
        BitmapCol* row = Bitmap_GetRow(bmp, y);

        for (int x = 0; x < width; x++) {
            BitmapCol col = row[x];

            //
            buffer24[buf_idx++] = BitmapCol_B(col);
            buffer24[buf_idx++] = BitmapCol_G(col);
            buffer24[buf_idx++] = BitmapCol_R(col);
        }
    }

    //
    _ksys_draw_bitmap(buffer24, 0, 0, width, height);

    //
}

void Window_FreeFramebuffer(struct Bitmap* bmp) {
    Mem_Free(bmp->scan0);
}

void OnscreenKeyboard_Open(struct OpenKeyboardArgs* args) { }
void OnscreenKeyboard_SetText(const cc_string* text) { }
void OnscreenKeyboard_Close(void) { }

void Window_EnableRawMouse(void) {
    DefaultEnableRawMouse();
}

void Window_UpdateRawMouse(void) {
    DefaultUpdateRawMouse();
}

void Window_DisableRawMouse(void) {
    DefaultDisableRawMouse();
}