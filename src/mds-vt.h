/**
 * mds — A micro-display server
 * Copyright © 2014  Mattias Andrée (maandree@member.fsf.org)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef MDS_MDS_VT_H
#define MDS_MDS_VT_H


#include "mds-base.h"

#include <linux/vt.h>


/**
 * Send a full message even if interrupted
 * 
 * @param   message  The message to send
 * @param   length   The length of the message
 * @return           Non-zero on success
 */
int full_send(const char* message, size_t length);


/**
 * Get the index of the next available virtual terminal
 * 
 * @return  -1 on error, 0 if the terminals are exhausted, otherwise the next terminal
 */
int vt_get_next_available(void);


/**
 * Get the currently active virtual terminal
 * 
 * @return  -1 on error, otherwise the current terminal
 */
int vt_get_active(void);


/**
 * Change currently active virtual terminal and wait for it to complete the switch
 * 
 * @param   vt  The index of the terminal
 * @return      Zero on success, -1 on error
 */
int vt_set_active(int vt);


/**
 * Open a virtual terminal
 * 
 * @param   vt        The index of the terminal
 * @param   old_stat  Output parameter for the old file stat for the terminal
 * @return            The file descriptor for the terminal, -1 on error
 */
int vt_open(int vt, struct stat* restrict old_stat);


/**
 * Close a virtual terminal
 * 
 * @param  vt        The index of the terminal
 * @param  old_stat  The old file stat for the terminal
 */
void vt_close(int fd, struct stat* restrict old_stat);



/**
 * Block or stop blocking other programs to open the a terminal
 * 
 * @param   fd:int         File descriptor for the terminal
 * @param   exclusive:int  Whether to block other programs for using the terminal
 * @return  :int           Zero on success, -1 on error
 */
#define vt_set_exclusive(fd, exclusive)  \
  (ioctl(fd, (exclusive) ? TIOCEXCL : TIOCNXCL))


/**
 * Configure a terminal to be set to either graphical mode or text mode
 * 
 * @param   fd:int         File descriptor for the terminal
 * @param   graphical:int  Whether to use graphical mode
 * @return  :int           Zero on success, -1 on error
 */
#define vt_set_graphical(fd, graphical)  \
  (ioctl(fd, KDSETMODE, (graphical) ? KD_GRAPHICS : KD_TEXT))


/**
 * Construct a virtual terminal mode that can be used in `vt_get_set_mode`
 * 
 * @param  vt_switch_control  Whether we want to be able to block and delay VT switches
 * @param  vt_leave_signal    The signal that should be send to us we a process is trying
 *                            to switch terminal to another terminal
 * @param  vt_enter_signal    The signal that should be send to us we a process is trying
 *                            to switch terminal to our terminal
 * @param  mode               Output parameter
 */
void vt_construct_mode(int vt_switch_control, int vt_leave_signal,
		       int vt_enter_signal, struct vt_mode* restrict mode);


/**
 * Set or get the mode for a virtual terminal
 * 
 * @param   fd:int                File descriptor for the terminal
 * @param   set:int               Whether to set the mode
 * @param   mode:struct vt_mode*  Input or outpur parameter for the mode
 * @return  :int                  Zero on success, -1 on error
 */
#define vt_get_set_mode(fd, set, mode)  \
  (ioctl(fd, set ? VT_SETMODE : VT_GETMODE, mode))


/**
 * Block or temporarily block virtual terminal switch
 * 
 * @param   fd:int  File descriptor for our terminal
 * @return  :int    Zero on success, -1 on error
 */
#define vt_stop_switch(fd)  \
  (ioctl(fd, VT_RELDISP, 0))


/**
 * Allow a temporarily block virtual terminal switch to continue
 * 
 * @param   fd:int  File descriptor for our terminal
 * @return  :int    Zero on success, -1 on error
 */
#define vt_continue_switch(fd)  \
  (ioctl(fd, VT_RELDISP, 1))


/**
 * Accept a virtual terminal switch
 * 
 * @param   fd:int  File descriptor for our terminal
 * @return  :int    Zero on success, -1 on error
 */
#define vt_accept_switch(fd)  \
  (ioctl(fd, VT_RELDISP, VT_ACKACQ))


#endif

