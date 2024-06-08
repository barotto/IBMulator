# IBMulator keymap system

A keymap is a file that defines mappings from keyboard, mouse, and game controllers input events to IBMulator events.

Along with simple key presses or axes motion, a keymap allows you to specify macros with multiple timed commands too.

## Bindings

For keyboard events IBMulator uses the SDL library's identifiers.
Refer to [libsdl.org](https://wiki.libsdl.org/SDL_Keycode) for the list of SDL symbols.

The general syntax for a binding line in the keymap file is:  
`INPUT_EVENT = IBMULATOR_EVENT [+IBMULATOR_EVENT...] [; OPTION...]`

`INPUT_EVENT` can be:

| Identifier           | Description
| ---------------------|------------
| `SDLK_*`             | SDL keyboard keycode symbol
| `SDL_SCANCODE_*`     | SDL keyboard scancode symbol
| `KMOD_*[+KMOD_*...]` | SDL keyboard modifier(s) followed by one of `SDLK_*` or `SDL_SCANCODE_*`
| `JOY_j_BUTTON_n`     | joystick / gamepad `j` button number `n` (j and n start from 0)
| `JOY_j_AXIS_n`       | joystick / gamepad `j` axis number `n` (j and n start from 0)
| `MOUSE_AXIS_a`       | mouse axis `a` (a=X or Y)
| `MOUSE_BUTTON_n`     | mouse button number `n` (1=left, 2=right, 3=center, other buttons have n>3)

`IBMULATOR_EVENT` can be:

| Identifier           | Description
| ---------------------|------------
| `KEY_*`              | an emulator (guest OS) keyboard key.
| `JOY_j_BUTTON_n`     | joystick `j` button number `n` (j=A or B, n=1 or 2).
| `JOY_j_AXIS_n`       | joystick `j` axis `n` (j=A or B, n=X or Y) (for params see below).
| `MOUSE_BUTTON_n`     | mouse button `n` (1=left, 2=center, 3=right).
| `MOUSE_AXIS_n(p,t,a)`| mouse axis `n` (n=X or Y) (for params see below).
| `FUNC_*`             | GUI, program, or emulator function (see below).
| `WAIT(x)`            | wait `x` milliseconds before firing the next event
| `RELEASE(i)`         | release key/button at index `i` (1-based); if i=0 or not specified then all keys will be released.
| `SKIP_TO(i)`         | skip event execution to event at index `i` (1-based).
| `REPEAT`             | equivalent to `SKIP_TO(1)`.
| `AUTOFIRE(x)`        | will be expanded to `WAIT(x/2) + RELEASE + WAIT(x/2) + REPEAT`; default for `x` is 50, ie. ~20 clicks per second.

`OPTION` can be:

* `MODE:m`: macro execution mode; possible values for `m` are:
    * `default`: default mode of execution, i.e. macro starts when the binding is activated and ends when deactivated.
    * `1shot`: macro starts and ends immediately; timed macros will run until their completion (endless macros will run forever).
    * `latched`: macro starts when the binding is activated and ends when it's activated again.
    * `repeat`: macro repeats after a 250ms delay, with a 20 repetitions per second frequency.
* `GROUP:g`: timed macros belonging to the same group `g` (a string) can't run concurrently.
* `TYPEMATIC:NO`: disables the typematic keyboard feature.

To specify multiple _binding options_ separate them with spaces.

SDL keycodes depend on the current keyboard layout loaded in your operating system.  
SDL scancodes represent physical keyboard keys and are not affected by any OS remapping.

`SKIP_TO` and `REPEAT` commands have an implicit minimum wait time dependent on the screen refresh rate.  
To avoid event spamming, they should be used in conjunction with `WAIT`.

Valid `KMOD_*` keyboard modifiers are:
* `KMOD_SHIFT`: any shift
* `KMOD_LSHIFT`: left shift
* `KMOD_RSHIFT`: right shift
* `KMOD_CTRL`: any control
* `KMOD_LCTRL`: left control
* `KMOD_RCTRL`: right control
* `KMOD_ALT`: any alt
* `KMOD_LALT`: left alt
* `KMOD_RALT`: right alt
* `KMOD_GUI`: any meta (the "Win" key)
* `KMOD_LGUI`: left meta
* `KMOD_RGUI`: right meta

Key modifiers bindings have some limitations:
* you must specify combos with the modifier(s) first, for example: `KMOD_CTRL + SDLK_TAB = KEY_ALT_L + KEY_TAB`.
* SDL key bindings for modifiers (e.g. `SDLK_LCTRL`) won't work as expected when their mode of operation is not `default` and they are also used in combos (e.g. `KMOD_CTRL + SDLK_TAB`).
* on keyboards with the `AltGr` key the `KMOD_RALT` modifier might not work, as `AltGr` could be translated with `LCTRL+RALT` by the host OS.

### Keyboard mappings

`KEY_*` events have an implicit chain of commands at the end to emulate typematic repeats, but only when the key event is not part of a macro (except when combined with key modifiers).

For example `SDLK_a = KEY_A` is automatically translated to:
```
SDLK_a = KEY_A + wait(tmd) + KEY_A + wait(tmr) + skip_to(3)
```
where:
* `tmd`: typematic delay
* `tmr`: typematic repeat rate

Both `tmd` and `tmr` values are set by the guest OS at run-time (see the `MODE` DOS command).

Keywords and identifiers are case insensitive, so for example `SDLK_a` is the same as `sdlk_a` and `SDLK_A`.

### Mouse mappings

`MOUSE_AXIS_n` events can have 3 arguments when mapped to joystick axes or buttons, all optional:
* argument 1 (`p`): amount of pixels / maximum speed of the mouse pointer (per 10ms, default 10).
* argument 2 (`t`): type of movement, 0=continuous/proportional, 1=accelerated, 2=single shot (default 0).
* argument 3 (`a`): amount of acceleration (default 5, applied only when t=1).

For buttons to mouse axis you must specify the direction with the sign of `p`; for joystick / gamepad axes to mouse axes the direction is auto determined and a negative `p` value will invert the direction.
These parameters won't apply when a mouse axis is mapped to a mouse axis; in that case a direct (relative movement) translation will be applied (only exception is the first parameter `p` that can be set to `-1` to invert the axis).

Examples:
* `SDLK_UP = MOUSE_AXIS_Y(10,1)`: accelerated movement with a max. speed of 10pix per 10ms.
* `SDLK_LEFT = MOUSE_AXIS_X(-5,0)`: -5pix per 10ms constant speed.
* `JOY_0_AXIS_0 = MOUSE_AXIS_X(10,0)`: proportional speed (smoothstep), 0 to +10/-10 pix per 10ms.
* `JOY_0_AXIS_1 = MOUSE_AXIS_Y(-10,0)`: the same as previous example, but with inverted axis.
* `JOY_0_AXIS_2 = MOUSE_AXIS_X(5,1)`: accelerated movement 0 to 5pix, useful for d-pads.

### Joystick mappings

`JOY_j_AXIS_n` events can have 3 arguments:
* argument 1 (`v`): stop value, from -32768 to 32767 (sign sets the direction); you can also use `-max` and `max` strings (no default).
* argument 2 (`t`): type of stick movement, 0=immediate, 1=constant speed (default 0).
* argument 3 (`a`): speed of stick movement (default 500, applied only when t=1).

Parameters `t` and `a` are applied only for keys and buttons; movement is automatically determined when you map mouse and joystick axes (a negative `v` value will invert the direction).

Examples:
* `SDLK_KP_2 = JOY_A_AXIS_Y(max,1)`: stick movement at constant speed from 0 to 32767.
* `SDLK_KP_4 = JOY_A_AXIS_X(-max,0)`: immediate stick movement to -32768.
* `SDLK_KP_6 = JOY_A_AXIS_X(max,1)`
* `SDLK_KP_8 = JOY_A_AXIS_Y(-max,1)`

### Function mappings

Valid `FUNC_* ` functions are:

| Identifier                  | Description
| --------------------------- |------------
| `FUNC_GUI_MODE_ACTION(x)`   | GUI Mode action number `x` (see below).
| `FUNC_SHOW_OPTIONS`         | open the options window.
| `FUNC_TOGGLE_MIXER`         | show / hide the mixer control window.
| `FUNC_TOGGLE_POWER`         | toggle the machine's power button.
| `FUNC_TOGGLE_PAUSE`         | pause / resume emulation
| `FUNC_TOGGLE_STATUS_IND`    | show / hide the status indicators.
| `FUNC_TOGGLE_DBG_WND`       | show / hide the debug windows.
| `FUNC_TAKE_SCREENSHOT`      | take a screenshot.
| `FUNC_TOGGLE_AUDIO_CAPTURE` | start / stop audio capture.
| `FUNC_TOGGLE_VIDEO_CAPTURE` | start / stop video capture.
| `FUNC_INSERT_MEDIUM`        | open the medium select dialog for the active drive.
| `FUNC_EJECT_MEDIUM`         | eject the medium inserted in the active drive.
| `FUNC_CHANGE_DRIVE`         | change the active drive (A->B->CD)
| `FUNC_SAVE_STATE`           | open the save state dialog.
| `FUNC_LOAD_STATE`           | open the load state dialog.
| `FUNC_QUICK_SAVE_STATE`     | save the state to the quicksave slot.
| `FUNC_QUICK_LOAD_STATE`     | load the state from the quicksave slot.
| `FUNC_GRAB_MOUSE`           | lock / unlock mouse to emulator.
| `FUNC_SYS_SPEED_UP`         | increase emulation speed (whole system).
| `FUNC_SYS_SPEED_DOWN`       | decrease emulation speed (whole system).
| `FUNC_SYS_SPEED(x)`         | set the emulation speed to `x`%.
| `FUNC_TOGGLE_FULLSCREEN`    | toggle fullscreen mode.
| `FUNC_SWITCH_KEYMAPS`       | change the active keymap to the next available one.
| `FUNC_SET_PREV_AUDIO_CH`    | switch to previous OSD audio channel.
| `FUNC_SET_NEXT_AUDIO_CH`    | switch to next OSD audio channel.
| `FUNC_SET_AUDIO_VOLUME(a)`  | change audio volume for the active OSD audio channel by `a`%.
| `FUNC_EXIT`                 | exit IBMulator.

For `FUNC_GUI_MODE_ACTION(x)`, the value of `x` determines the action:
* `x`=1:
  * in Compact mode: toggle the main interface window
  * in Realistic mode: toggle zoomed view
* `x`=2:
  * in Normal and Compact modes: switch between Normal and Compact modes
  * in Realistic mode: switch between bright and dark styles

## Macro examples

Joystick autofire with a 500ms delay and ~20 clicks per second:
```
JOY_0_BUTTON_0 = JOY_A_BUTTON_1 + wait(500) + release + wait(25) + JOY_A_BUTTON_1 + wait(25) + skip_to(3)
```

Normal 'D' key press, but if kept pressed for >250ms runs the DOS `dir` command instead of typematic repeats:
```
SDLK_d = KEY_D + wait(250) + KEY_I + KEY_R + KEY_ENTER
```

