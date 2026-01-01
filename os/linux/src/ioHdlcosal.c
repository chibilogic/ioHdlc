/**
 * @file    ioHdlcosal.c
 * @brief   OSAL implementation for Linux/POSIX.
 */

#define _POSIX_C_SOURCE 200809L
#include "ioHdlcosal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*===========================================================================*/
/* Thread-Local Events (ChibiOS-like event model)                           */
/*===========================================================================*/

/**
 * @brief   Thread-local storage key for per-thread events.
 */
static pthread_key_t thread_events_key;
static pthread_once_t thread_events_key_once = PTHREAD_ONCE_INIT;

/**
 * @brief   Destructor for thread-local events.
 */
static void thread_events_destructor(void *data) {
  if (data) {
    iohdlc_thread_events_t *tev = (iohdlc_thread_events_t *)data;
    pthread_mutex_destroy(&tev->lock);
    pthread_cond_destroy(&tev->cond);
    free(tev);
  }
}

/**
 * @brief   Initialize thread-local storage key (once).
 */
static void thread_events_key_init(void) {
  pthread_key_create(&thread_events_key, thread_events_destructor);
}

/**
 * @brief   Get or create thread-local event state.
 */
static iohdlc_thread_events_t* get_thread_events(void) {
  pthread_once(&thread_events_key_once, thread_events_key_init);
  
  iohdlc_thread_events_t *tev = pthread_getspecific(thread_events_key);
  if (!tev) {
    tev = calloc(1, sizeof(iohdlc_thread_events_t));
    pthread_mutex_init(&tev->lock, NULL);
    pthread_cond_init(&tev->cond, NULL);
    tev->pending = 0;
    pthread_setspecific(thread_events_key, tev);
  }
  return tev;
}

/*===========================================================================*/
/* Binary Semaphore Implementation                                          */
/*===========================================================================*/

/**
 * @brief   Wait on binary semaphore with timeout.
 * @details Supports infinite, immediate, and timed waits.
 *
 * @param[in] bsem      Pointer to binary semaphore
 * @param[in] timeout   Timeout in milliseconds
 * @return              0 on success, -ETIMEDOUT on timeout
 */
int iohdlc_bsem_wait_timeout(iohdlc_bsem_t *bsem, iohdlc_systime_t timeout) {
  if (timeout == IOHDLC_TIME_INFINITE) {
    /* Blocking wait */
    if (sem_wait(&bsem->sem) == 0) {
      return 0;
    }
    return -errno;
  }
  else if (timeout == IOHDLC_TIME_IMMEDIATE) {
    /* Non-blocking try */
    if (sem_trywait(&bsem->sem) == 0) {
      return 0;
    }
    return (errno == EAGAIN) ? -ETIMEDOUT : -errno;
  }
  else {
    /* Timed wait */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    
    /* Add timeout milliseconds */
    ts.tv_sec += timeout / 1000;
    ts.tv_nsec += (timeout % 1000) * 1000000L;
    
    /* Handle nanosecond overflow */
    if (ts.tv_nsec >= 1000000000L) {
      ts.tv_sec += 1;
      ts.tv_nsec -= 1000000000L;
    }
    
    if (sem_timedwait(&bsem->sem, &ts) == 0) {
      return 0;
    }
    return (errno == ETIMEDOUT) ? -ETIMEDOUT : -errno;
  }
}

/*===========================================================================*/
/* Memory Allocation                                                         */
/*===========================================================================*/

void* iohdlc_alloc(size_t size) {
  return malloc(size);
}

void iohdlc_free(void* ptr) {
  free(ptr);
}

/*===========================================================================*/
/* System Time                                                               */
/*===========================================================================*/

iohdlc_systime_t iohdlc_get_systime(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (iohdlc_systime_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

/*===========================================================================*/
/* Virtual Timer Implementation                                              */
/*===========================================================================*/

/**
 * @brief   Timer signal handler (called by POSIX timer).
 */
static void timer_signal_handler(union sigval sv) {
  iohdlc_virtual_timer_t *vtp = (iohdlc_virtual_timer_t *)sv.sival_ptr;
  
  pthread_mutex_lock(&vtp->lock);
  
  if (vtp->armed) {
    vtp->armed = false;
    vtp->expired = true;
    
    /* Call user callback */
    if (vtp->callback) {
      vtp->callback(vtp, vtp->par);
    }
  }
  
  pthread_mutex_unlock(&vtp->lock);
}

void iohdlc_vt_init(iohdlc_virtual_timer_t *vtp) {
  struct sigevent sev;
  
  memset(vtp, 0, sizeof(*vtp));
  pthread_mutex_init(&vtp->lock, NULL);
  
  /* Create POSIX timer */
  memset(&sev, 0, sizeof(sev));
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = timer_signal_handler;
  sev.sigev_value.sival_ptr = vtp;
  
  if (timer_create(CLOCK_MONOTONIC, &sev, &vtp->timer_id) == -1) {
    /* Fallback to CLOCK_REALTIME if MONOTONIC not available */
    timer_create(CLOCK_REALTIME, &sev, &vtp->timer_id);
  }
  
  vtp->armed = false;
  vtp->expired = false;
}

void iohdlc_vt_set(iohdlc_virtual_timer_t *vtp, uint32_t delay_ms,
                   iohdlc_vt_callback_t callback, void *par) {
  struct itimerspec its;
  
  pthread_mutex_lock(&vtp->lock);
  
  /* Cancel any existing timer */
  if (vtp->armed) {
    memset(&its, 0, sizeof(its));
    timer_settime(vtp->timer_id, 0, &its, NULL);
  }
  
  /* Set new timer */
  vtp->callback = callback;
  vtp->par = par;
  vtp->armed = true;
  vtp->expired = false;
  
  memset(&its, 0, sizeof(its));
  its.it_value.tv_sec = delay_ms / 1000;
  its.it_value.tv_nsec = (delay_ms % 1000) * 1000000L;
  
  timer_settime(vtp->timer_id, 0, &its, NULL);
  
  pthread_mutex_unlock(&vtp->lock);
}

void iohdlc_vt_reset(iohdlc_virtual_timer_t *vtp) {
  struct itimerspec its;
  
  pthread_mutex_lock(&vtp->lock);
  
  if (vtp->armed) {
    memset(&its, 0, sizeof(its));
    timer_settime(vtp->timer_id, 0, &its, NULL);
    vtp->armed = false;
  }
  vtp->expired = false;
  
  pthread_mutex_unlock(&vtp->lock);
}

bool iohdlc_vt_is_armed(iohdlc_virtual_timer_t *vtp) {
  bool armed;
  pthread_mutex_lock(&vtp->lock);
  armed = vtp->armed;
  pthread_mutex_unlock(&vtp->lock);
  return armed;
}

/*===========================================================================*/
/* Event Source Implementation                                               */
/*===========================================================================*/

void iohdlc_evt_init(iohdlc_event_source_t *esp) {
  memset(esp, 0, sizeof(*esp));
  pthread_mutex_init(&esp->lock, NULL);
  esp->listeners = NULL;
}

void iohdlc_evt_broadcast_flags(iohdlc_event_source_t *esp, eventflags_t flags) {
  iohdlc_event_listener_t *elp;
  
  pthread_mutex_lock(&esp->lock);
  
  /* Notify all listeners */
  for (elp = esp->listeners; elp != NULL; elp = elp->next) {
    pthread_mutex_lock(&elp->lock);
    elp->flags |= (flags & elp->wflags);
    
    /* Also signal thread events if listener wants them */
    if (elp->flags & elp->wflags) {
      /* Set pending in thread event state */
      if (elp->thread_events) {
        pthread_mutex_lock(&elp->thread_events->lock);
        elp->thread_events->pending |= elp->events;
        pthread_cond_broadcast(&elp->thread_events->cond);
        pthread_mutex_unlock(&elp->thread_events->lock);
      }
    }
    
    pthread_mutex_unlock(&elp->lock);
  }
  
  pthread_mutex_unlock(&esp->lock);
}

/*===========================================================================*/
/* Event Listener Implementation                                             */
/*===========================================================================*/

void iohdlc_evt_register(iohdlc_event_source_t *esp,
                         iohdlc_event_listener_t *elp,
                         eventmask_t events,
                         eventflags_t wflags) {
  memset(elp, 0, sizeof(*elp));
  elp->thread = pthread_self();
  elp->thread_events = get_thread_events();
  elp->events = events;
  elp->wflags = wflags;
  elp->flags = 0;
  pthread_mutex_init(&elp->lock, NULL);
  
  /* Add to listener chain */
  pthread_mutex_lock(&esp->lock);
  elp->next = esp->listeners;
  esp->listeners = elp;
  pthread_mutex_unlock(&esp->lock);
}

void iohdlc_evt_unregister(iohdlc_event_source_t *esp,
                           iohdlc_event_listener_t *elp) {
  iohdlc_event_listener_t **pp;
  
  pthread_mutex_lock(&esp->lock);
  
  /* Remove from listener chain */
  for (pp = &esp->listeners; *pp != NULL; pp = &(*pp)->next) {
    if (*pp == elp) {
      *pp = elp->next;
      break;
    }
  }
  
  pthread_mutex_unlock(&esp->lock);
  
  /* Cleanup listener */
  pthread_mutex_destroy(&elp->lock);
}

eventmask_t iohdlc_evt_wait_any_timeout(eventmask_t events, uint32_t timeout_ms) {
  iohdlc_thread_events_t *tev = get_thread_events();
  eventmask_t matched = 0;
  
  pthread_mutex_lock(&tev->lock);
  
  /* Check if events already pending */
  matched = tev->pending & events;
  
  if (matched == 0) {
    /* No events yet, wait */
    if (timeout_ms == 0) {
      /* Non-blocking */
      pthread_mutex_unlock(&tev->lock);
      return 0;
    } else if (timeout_ms == IOHDLC_TIME_INFINITE) {
      /* Wait forever */
      while ((matched = (tev->pending & events)) == 0) {
        pthread_cond_wait(&tev->cond, &tev->lock);
      }
    } else {
      /* Timed wait */
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += timeout_ms / 1000;
      ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
      if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
      }
      
      while ((matched = (tev->pending & events)) == 0) {
        int result = pthread_cond_timedwait(&tev->cond, &tev->lock, &ts);
        if (result == ETIMEDOUT) {
          break;
        }
      }
    }
  }
  
  /* Clear matched events */
  tev->pending &= ~matched;
  
  pthread_mutex_unlock(&tev->lock);
  
  return matched;
}

void iohdlc_evt_signal(eventmask_t events) {
  iohdlc_thread_events_t *tev = get_thread_events();
  
  pthread_mutex_lock(&tev->lock);
  tev->pending |= events;
  pthread_cond_broadcast(&tev->cond);
  pthread_mutex_unlock(&tev->lock);
}

eventflags_t iohdlc_evt_get_and_clear_flags(iohdlc_event_listener_t *elp) {
  eventflags_t flags;
  
  pthread_mutex_lock(&elp->lock);
  flags = elp->flags;
  elp->flags = 0;
  pthread_mutex_unlock(&elp->lock);
  
  return flags;
}
