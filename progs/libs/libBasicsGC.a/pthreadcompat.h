/* Copyright (C) 2001, Compaq Computer Corporation

** This file is part of Vesta.

** Vesta is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License as published by the Free Software Foundation; either
** version 2.1 of the License, or (at your option) any later version.

** Vesta is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Lesser General Public License for more details.

** You should have received a copy of the GNU Lesser General Public
** License along with Vesta; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

** Last modified on Wed Nov  7 14:40:45 EST 2001 by ken@xorian.net
**/

/* This header file provides backwards compatibility with the old version
   of the pthread library available on OSF prior to 4.0.*/

#include <pthread.h>

#ifndef _PTHREAD_D4_
#if (defined(__osf__) || defined(WIN32)) && defined(PTHREAD)
#define _PTHREAD_D4_
#endif
#endif

#ifdef _PTHREAD_D4_

#define pthread_attr_init pthread_attr_create

#define pthread_mutexattr_init pthread_mutexattr_create

#define pthread_detach(t) pthread_detach(&(t))

#define pthread_create(a,b,c,d)\
pthread_create((a),\
(((b)==0)\
 ? pthread_attr_default\
 : *(b)),\
(c),(d))

#define pthread_cond_init(a,b)\
pthread_cond_init((a),\
(((b)==0)\
 ? pthread_condattr_default\
 : *(b)))

#define pthread_mutex_init(a,b)\
pthread_mutex_init((a),\
(((b)==0)\
 ? pthread_mutexattr_default\
 : *(b)))

#define pthread_join(t,r)\
(pthread_join((t),(r))==0?pthread_detach(t):-1)

#define PTHREAD_ONCE_INIT pthread_once_init

#endif // _PTHREAD_D4_

// minimum stack size
#ifndef PTHREAD_STACK_MIN
#define PTHREAD_STACK_MIN 10
#endif
