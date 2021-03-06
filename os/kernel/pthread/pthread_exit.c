/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
/************************************************************************
 * kernel/pthread/pthread_exit.c
 *
 *   Copyright (C) 2007, 2009, 2011-2013 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ************************************************************************/

/************************************************************************
 * Included Files
 ************************************************************************/

#include <tinyara/config.h>

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <debug.h>

#include <tinyara/arch.h>

#include "sched/sched.h"
#include "task/task.h"
#include "pthread/pthread.h"

/************************************************************************
 * Definitions
 ************************************************************************/

/************************************************************************
 * Private Type Declarations
 ************************************************************************/

/************************************************************************
 * Global Variables
 ************************************************************************/

/************************************************************************
 * Private Variables
 ************************************************************************/

/************************************************************************
 * Private Functions
 ************************************************************************/

/************************************************************************
 * Public Functions
 ************************************************************************/

/************************************************************************
 * Name: pthread_exit
 *
 * Description:
 *   Terminate execution of a thread started with pthread_create.
 *
 * Parameters:
 *   exit_valie
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *
 ************************************************************************/

void pthread_exit(FAR void *exit_value)
{
#if CONFIG_NPTHREAD_KEYS > 0
	struct pthread_tcb_s *rptcb = (struct pthread_tcb_s *)this_task();
	struct pthread_key_s *key;
	int destr_count = 0;
	int key_index;
#endif
	struct tcb_s *tcb = this_task();
	int status;

	svdbg("exit_value=%p\n", exit_value);

	/* Block any signal actions that would awaken us while were
	 * are performing the JOIN handshake.
	 */

#ifndef CONFIG_DISABLE_SIGNALS
	{
		sigset_t set = ALL_SIGNAL_SET;
		(void)sigprocmask(SIG_SETMASK, &set, NULL);
	}
#endif

#ifdef CONFIG_CANCELLATION_POINTS
	/* Mark the pthread as non-cancelable to avoid additional calls to
	 * pthread_exit() due to any cancellation point logic that might get
	 * kicked off by actions taken during pthread_exit processing.
	 */
	tcb->flags |= TCB_FLAG_NONCANCELABLE;
	tcb->flags &= ~TCB_FLAG_CANCEL_PENDING;
	tcb->cpcount = 0;
#endif

#ifdef CONFIG_PTHREAD_CLEANUP
	/* Perform any stack pthread clean-up callbacks */
	pthread_cleanup_popall((FAR struct pthread_tcb_s *)tcb);
#endif

#if CONFIG_NPTHREAD_KEYS > 0
	for (key_index = 0; key_index < PTHREAD_KEYS_MAX; key_index++) {
		key = &rptcb->pthread_data[key_index];
		if (key->destructor != NULL && key->data != NULL) {
			key->destructor(key->data);
			destr_count++;
		}
		if (destr_count >= PTHREAD_DESTRUCTOR_ITERATIONS) {
			break;
		}
	}
#endif

	/* Complete pending join operations */

	status = pthread_completejoin(getpid(), exit_value);
	if (status != OK) {
		/* Assume that the join completion failured because this
		 * not really a pthread.  Exit by calling exit().
		 */

		exit(EXIT_FAILURE);
	}
	
#ifndef CONFIG_PTHREAD_MUTEX_UNSAFE
	/* Recover any mutexes still held by the canceled thread */
	pthread_mutex_inconsistent((FAR struct pthread_tcb_s *)tcb);
#endif

	/* Perform common task termination logic.  This will get called again later
	 * through logic kicked off by _exit().  However, we need to call it before
	 * calling _exit() in order certain operations if this is the last thread
	 * of a task group:  (2) To handle atexit() and on_exit() callbacks and
	 * (2) so that we can flush buffered I/O (which may required suspending).
	 */

	task_exithook(tcb, EXIT_SUCCESS, false);

	/* Then just exit, retaining all file descriptors and without
	 * calling atexit() functions.
	 */

	_exit(EXIT_SUCCESS);
}
