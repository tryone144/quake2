/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

/*
 * Text-mode renderer using AA-lib
 *
 * version 0.2
 *
 * Jacek Fedorynski <jfedor@jfedor.org>
 *
 * http://www.jfedor.org/aaquake2/
 *
 * Modified by Bernd Busse
 *
 * This file is a modified rw_in_svgalib.c.
 *
 * Graphics stuff is in rw_aa.c.
 *
 * Changelog:
 *
 *      2015-02-10      0.2    Mouse support
 *
 *      2001-12-30      0.1    Initial release
 *
 */

#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <sys/mman.h>

#if defined (__linux__)
#include <sys/io.h>
#include <sys/vt.h>
#endif

#include <aalib.h>

#include "../ref_soft/r_local.h"
#include "../client/keys.h"
#include "../linux/rw_linux.h"

extern aa_context *aac;

// Mouse control for KBD
static void set_mouseevent(void);
static void init_mouse(void);
static void uninit_mouse(void);

/*****************************************************************************/
/* KEYBOARD                                                                  */
/*****************************************************************************/

Key_Event_fp_t Key_Event_fp;

void KBD_Init(Key_Event_fp_t fp)
{
    Key_Event_fp = fp;
    if (!aac)
        Sys_Error("aac is NULL\n");

    if (!aa_autoinitkbd(aac, AA_SENDRELEASE))
        Sys_Error("aa_autoinitkbd() failed\n");
        
    init_mouse(); // Do aa_mouseinit
}

void KBD_Update(void)
{
    int ev;
    int down;
    
    while (ev = aa_getevent(aac, 0)) {
        down = 1;
release:
        switch (ev) {
            case AA_UP:
                Key_Event_fp(K_UPARROW, down);
                break;
            case AA_DOWN:
                Key_Event_fp(K_DOWNARROW, down);
                break;
            case AA_LEFT:
                Key_Event_fp(K_LEFTARROW, down);
                break;
            case AA_RIGHT:
                Key_Event_fp(K_RIGHTARROW, down);
                break;
            case AA_BACKSPACE:
                Key_Event_fp(K_BACKSPACE, down);
                break;
            case AA_ESC:
                Key_Event_fp(K_ESCAPE, down);
                break;
            case AA_MOUSE:
                set_mouseevent(); // let the mousedriver handle this
                break;
        }
        if (ev < 256) {
            Key_Event_fp(ev, down);
        } else if (ev > AA_RELEASE) {
            ev &= ~AA_RELEASE;
            down = 0;
            goto release;
        }
    }
}

void KBD_Close(void)
{
    if (!aac)
        Sys_Error("aac is NULL\n");

    uninit_mouse(); // Do aa_mouseuninit
    aa_uninitkbd(aac);
}

/*****************************************************************************/
/* MOUSE                                                                     */
/*****************************************************************************/

// this is inside the renderer shared lib, so these are called from vid_so


/*
static int      mouserate = MOUSE_DEFAULTSAMPLERATE;

static int     mouse_buttons;
static int     mouse_buttonstate;
static int     mouse_oldbuttonstate;
static float   mouse_x, mouse_y;
static float    old_mouse_x, old_mouse_y;
static int      mx, my;

static cvar_t   *m_filter;
static cvar_t   *in_mouse;

static cvar_t   *mdev;
static cvar_t   *mrate;

static qboolean mlooking;

static cvar_t *sensitivity;
static cvar_t *lookstrafe;
static cvar_t *m_side;
static cvar_t *m_yaw;
static cvar_t *m_pitch;
static cvar_t *m_forward;
static cvar_t *freelook;
*/

static qboolean UseMouse = false;

static int mouse_x, mouse_y;
static int mouse_oldx, mouse_oldy;
static int mx, my;

static int mouse_buttonstate;
static int mouse_oldbuttonstate;

//static int mwheel;

static qboolean mlooking;

static cvar_t *sensitivity;
static cvar_t *lookstrafe;
static cvar_t *m_side;
static cvar_t *m_yaw;
static cvar_t *m_pitch;
static cvar_t *m_forward;
static cvar_t *freelook;

// state struct passed in Init
static in_state_t   *in_state;

/* Keyboard -> Mouse Handler */
static void set_mouseevent(void)
{
    aa_getmouse(aac, &mouse_x, &mouse_y, &mouse_buttonstate);
    mx += mouse_oldx - mouse_x;
    my += mouse_oldy - mouse_y;
    mouse_oldx = mouse_x;
    mouse_oldy = mouse_y;
}

static void init_mouse(void)
{
    if (!aac)
        Sys_Error("aac is NULL\n");

    if (!aa_autoinitmouse(aac, AA_MOUSEALLMASK))
        Sys_Error("aa_autoinitmouse() failed\n");
    
    UseMouse = true;
    
    aa_hidemouse(aac);
}

static void uninit_mouse(void)
{
    if (!aac)
        Sys_Error("aac is NULL\n");
    
    UseMouse = false;
    
    aa_showmouse(aac);
    aa_uninitmouse(aac);
}

/* Callback functions */
static void Force_CenterView_f (void)
{
    in_state->viewangles[PITCH] = 0;
}

static void RW_IN_MLookDown (void) 
{ 
  mlooking = true; 
}

static void RW_IN_MLookUp (void) 
{
  mlooking = false;
  in_state->IN_CenterView_fp ();
}

void RW_IN_Init(in_state_t *in_state_p)
{
    in_state = in_state_p;
    
    freelook = ri.Cvar_Get( "freelook", "0", 0 );
    lookstrafe = ri.Cvar_Get ("lookstrafe", "0", 0);
    sensitivity = ri.Cvar_Get ("sensitivity", "3", 0);
    m_pitch = ri.Cvar_Get ("m_pitch", "0.022", 0);
    m_yaw = ri.Cvar_Get ("m_yaw", "0.022", 0);
    m_forward = ri.Cvar_Get ("m_forward", "1", 0);
    m_side = ri.Cvar_Get ("m_side", "0.8", 0);
    
    ri.Cmd_AddCommand ("+mlook", RW_IN_MLookDown);
    ri.Cmd_AddCommand ("-mlook", RW_IN_MLookUp);

    ri.Cmd_AddCommand ("force_centerview", Force_CenterView_f);
    
/*
    // mouse variables
    m_filter = ri.Cvar_Get ("m_filter", "0", 0);
    in_mouse = ri.Cvar_Get ("in_mouse", "1", CVAR_ARCHIVE);
    freelook = ri.Cvar_Get( "freelook", "0", 0 );
    lookstrafe = ri.Cvar_Get ("lookstrafe", "0", 0);
    sensitivity = ri.Cvar_Get ("sensitivity", "3", 0);
    m_pitch = ri.Cvar_Get ("m_pitch", "0.022", 0);
    m_yaw = ri.Cvar_Get ("m_yaw", "0.022", 0);
    m_forward = ri.Cvar_Get ("m_forward", "1", 0);
    m_side = ri.Cvar_Get ("m_side", "0.8", 0);
*/
}

void RW_IN_Shutdown(void)
{
    if (!UseMouse)
        return;
        
    uninit_mouse();
}

/*
===========
IN_Commands
===========
*/
void RW_IN_Commands (void)
{
    if (!UseMouse)
        return;

    // perform button actions
    if ((mouse_buttonstate & AA_BUTTON1) && !(mouse_oldbuttonstate & AA_BUTTON1))
        in_state->Key_Event_fp (K_MOUSE1, true);
    else if (!(mouse_buttonstate & AA_BUTTON1) && (mouse_oldbuttonstate & AA_BUTTON1))
        in_state->Key_Event_fp (K_MOUSE1, false);

    if ((mouse_buttonstate & AA_BUTTON3) && !(mouse_oldbuttonstate & AA_BUTTON3))
        in_state->Key_Event_fp (K_MOUSE2, true);
    else if (!(mouse_buttonstate & AA_BUTTON3) && (mouse_oldbuttonstate & AA_BUTTON3))
        in_state->Key_Event_fp (K_MOUSE2, false);

    if ((mouse_buttonstate & AA_BUTTON2) && !(mouse_oldbuttonstate & AA_BUTTON2))
        in_state->Key_Event_fp (K_MOUSE3, true);
    else if (!(mouse_buttonstate & AA_BUTTON2) && (mouse_oldbuttonstate & AA_BUTTON2))
        in_state->Key_Event_fp (K_MOUSE3, false);
    
    mouse_oldbuttonstate = mouse_buttonstate;

/*
    if (mwheel < 0) {
        in_state->Key_Event_fp (K_MWHEELUP, true);
        in_state->Key_Event_fp (K_MWHEELUP, false);
    }
    if (mwheel > 0) {
        in_state->Key_Event_fp (K_MWHEELDOWN, true);
        in_state->Key_Event_fp (K_MWHEELDOWN, false);
    }   
    mwheel = 0;
*/
}

/*
===========
IN_Move
===========
*/
void RW_IN_Move (usercmd_t *cmd)
{
    if (!UseMouse)
        return;

    // TODO: Add filtering
    /*if (m_filter->value)
    {
        mouse_x = (mx + old_mouse_x) * 0.5;
        mouse_y = (my + old_mouse_y) * 0.5;
    }
    else
    {*/
    int dx = mx*9;
    int dy = my*16;
    
    if (!mx && !my)
        return;

    mx = my = 0; // clear for next update

    dx *= sensitivity->value;
    dy *= sensitivity->value;

    // add mousemovement to cmd
    if ( (*in_state->in_strafe_state & 1) || (lookstrafe->value && mlooking) )
        cmd->sidemove += m_side->value * dx;
    else
        in_state->viewangles[YAW] -= m_yaw->value * dx;

    if ( (mlooking || freelook->value) && !(*in_state->in_strafe_state & 1) )
        in_state->viewangles[PITCH] += m_pitch->value * dy;
    else
        cmd->forwardmove -= m_forward->value * dy;
}

void RW_IN_Frame (void)
{
}

void RW_IN_Activate(void)
{
}

