/**
 * @file    mock_stream.c
 * @brief   Mock stream implementation.
 */

#define _DEFAULT_SOURCE
#include "mock_stream.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <stdio.h>

/*===========================================================================*/
/* Buffer Operations                                                         */
/*===========================================================================*/

static void buffer_init(mock_buffer_t *buf) {
  memset(buf->data, 0, sizeof buf->data);
  buf->head = 0;
  buf->tail = 0;
  buf->count = 0;
  pthread_mutex_init(&buf->lock, NULL);
  pthread_cond_init(&buf->not_empty, NULL);
  pthread_cond_init(&buf->not_full, NULL);
}

static void buffer_destroy(mock_buffer_t *buf) {
  pthread_mutex_destroy(&buf->lock);
  pthread_cond_destroy(&buf->not_empty);
  pthread_cond_destroy(&buf->not_full);
}

static size_t buffer_write(mock_buffer_t *buf, const uint8_t *data, size_t size, int timeout_ms) {
  struct timespec ts;
  struct timeval now;
  size_t written = 0;

  pthread_mutex_lock(&buf->lock);

  while (written < size) {
    /* Wait for space if buffer full */
    while (buf->count >= MOCK_STREAM_BUFFER_SIZE) {
      if (timeout_ms == 0) {
        pthread_mutex_unlock(&buf->lock);
        return written;
      }
      
      if (timeout_ms > 0) {
        gettimeofday(&now, NULL);
        ts.tv_sec = now.tv_sec + (timeout_ms / 1000);
        ts.tv_nsec = (now.tv_usec * 1000) + ((timeout_ms % 1000) * 1000000);
        if (ts.tv_nsec >= 1000000000) {
          ts.tv_sec++;
          ts.tv_nsec -= 1000000000;
        }
        
        if (pthread_cond_timedwait(&buf->not_full, &buf->lock, &ts) == ETIMEDOUT) {
          pthread_mutex_unlock(&buf->lock);
          return written;
        }
      } else {
        pthread_cond_wait(&buf->not_full, &buf->lock);
      }
    }

    /* Write one byte */
    buf->data[buf->tail] = data[written];
    buf->tail = (buf->tail + 1) % MOCK_STREAM_BUFFER_SIZE;
    buf->count++;
    written++;
  }

  pthread_cond_signal(&buf->not_empty);
  pthread_mutex_unlock(&buf->lock);

  return written;
}

static size_t buffer_read(mock_buffer_t *buf, uint8_t *data, size_t size, int timeout_ms) {
  struct timespec ts;
  struct timeval now;
  size_t read_bytes = 0;

  pthread_mutex_lock(&buf->lock);

  /* Wait for data if buffer empty */
  while (buf->count == 0) {
    if (timeout_ms == 0) {
      pthread_mutex_unlock(&buf->lock);
      return 0;
    }
    
    if (timeout_ms > 0) {
      gettimeofday(&now, NULL);
      ts.tv_sec = now.tv_sec + (timeout_ms / 1000);
      ts.tv_nsec = (now.tv_usec * 1000) + ((timeout_ms % 1000) * 1000000);
      if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
      }
      
      if (pthread_cond_timedwait(&buf->not_empty, &buf->lock, &ts) == ETIMEDOUT) {
        pthread_mutex_unlock(&buf->lock);
        return 0;
      }
    } else {
      pthread_cond_wait(&buf->not_empty, &buf->lock);
    }
  }

  /* Read available bytes (up to size) */
  while (read_bytes < size && buf->count > 0) {
    data[read_bytes] = buf->data[buf->head];
    buf->head = (buf->head + 1) % MOCK_STREAM_BUFFER_SIZE;
    buf->count--;
    read_bytes++;
  }

  pthread_cond_signal(&buf->not_full);
  pthread_mutex_unlock(&buf->lock);

  return read_bytes;
}

/*===========================================================================*/
/* Mock Stream Implementation                                                */
/*===========================================================================*/

mock_stream_t* mock_stream_create(const mock_stream_config_t *config) {
  mock_stream_t *stream = calloc(1, sizeof(mock_stream_t));
  if (!stream) {
    return NULL;
  }

  buffer_init(&stream->rx_buf);
  buffer_init(&stream->tx_buf);
  pthread_mutex_init(&stream->state_lock, NULL);

  if (config) {
    stream->config = *config;
  } else {
    /* Default configuration */
    stream->config.loopback = false;
    stream->config.inject_errors = false;
    stream->config.error_rate = 0;
    stream->config.delay_us = 0;
  }

  stream->peer = NULL;
  stream->closed = false;

  return stream;
}

void mock_stream_destroy(mock_stream_t *stream) {
  if (!stream) {
    return;
  }

  mock_stream_disconnect(stream);
  
  buffer_destroy(&stream->rx_buf);
  buffer_destroy(&stream->tx_buf);
  pthread_mutex_destroy(&stream->state_lock);
  
  free(stream);
}

void mock_stream_connect(mock_stream_t *stream_a, mock_stream_t *stream_b) {
  if (!stream_a || !stream_b) {
    return;
  }

  pthread_mutex_lock(&stream_a->state_lock);
  pthread_mutex_lock(&stream_b->state_lock);

  stream_a->peer = stream_b;
  stream_b->peer = stream_a;

  pthread_mutex_unlock(&stream_b->state_lock);
  pthread_mutex_unlock(&stream_a->state_lock);
}

void mock_stream_disconnect(mock_stream_t *stream) {
  if (!stream) {
    return;
  }

  pthread_mutex_lock(&stream->state_lock);
  
  if (stream->peer) {
    pthread_mutex_lock(&stream->peer->state_lock);
    stream->peer->peer = NULL;
    pthread_mutex_unlock(&stream->peer->state_lock);
    stream->peer = NULL;
  }

  pthread_mutex_unlock(&stream->state_lock);
}

ssize_t mock_stream_read(mock_stream_t *stream, uint8_t *buf, size_t size, int timeout_ms) {
  if (!stream || stream->closed) {
    return -1;
  }

  return buffer_read(&stream->rx_buf, buf, size, timeout_ms);
}

ssize_t mock_stream_write(mock_stream_t *stream, const uint8_t *buf, size_t size, int timeout_ms) {
  if (!stream || stream->closed) {
    return -1;
  }

  /* Simulate delay if configured */
  if (stream->config.delay_us > 0) {
    usleep(stream->config.delay_us);
  }

  pthread_mutex_lock(&stream->state_lock);

  /* Apply error injection if configured */
  uint8_t *data_to_send = (uint8_t *)buf;
  uint8_t corrupted_data[size];
  
  if (stream->config.inject_errors && stream->config.error_rate > 0) {
    memcpy(corrupted_data, buf, size);
    for (size_t i = 0; i < size; i++) {
      /* Generate random number 0-999 */
      uint32_t rnd = (uint32_t)rand() % 1000;
      if (rnd < stream->config.error_rate) {
        /* Flip a random bit in this byte */
        uint8_t bit = rand() % 8;
        corrupted_data[i] ^= (1 << bit);
      }
    }
    data_to_send = corrupted_data;
  }

  ssize_t written;
  
  /* Loopback: write to own TX buffer, then copy to own RX */
  if (stream->config.loopback) {
    pthread_mutex_unlock(&stream->state_lock);
    written = buffer_write(&stream->tx_buf, data_to_send, size, timeout_ms);
    if (written > 0) {
      pthread_mutex_lock(&stream->state_lock);
      buffer_write(&stream->rx_buf, data_to_send, written, 0);
      pthread_mutex_unlock(&stream->state_lock);
    }
  }
  /* Peer connection: write directly to peer's RX (skip own TX buffer) */
  else if (stream->peer) {
    mock_stream_t *peer = stream->peer;
    pthread_mutex_unlock(&stream->state_lock);
    written = buffer_write(&peer->rx_buf, data_to_send, size, timeout_ms);
  }
  /* No loopback, no peer: write to own TX buffer only */
  else {
    pthread_mutex_unlock(&stream->state_lock);
    written = buffer_write(&stream->tx_buf, data_to_send, size, timeout_ms);
  }

  return written;
}

size_t mock_stream_available(mock_stream_t *stream) {
  if (!stream) {
    return 0;
  }

  pthread_mutex_lock(&stream->rx_buf.lock);
  size_t count = stream->rx_buf.count;
  pthread_mutex_unlock(&stream->rx_buf.lock);

  return count;
}

size_t mock_stream_inject_rx(mock_stream_t *stream, const uint8_t *data, size_t size) {
  if (!stream) {
    return 0;
  }

  return buffer_write(&stream->rx_buf, data, size, 0);
}

size_t mock_stream_drain_tx(mock_stream_t *stream, uint8_t *buf, size_t size) {
  if (!stream) {
    return 0;
  }

  return buffer_read(&stream->tx_buf, buf, size, 0);
}

void mock_stream_clear(mock_stream_t *stream) {
  if (!stream) {
    return;
  }

  pthread_mutex_lock(&stream->rx_buf.lock);
  stream->rx_buf.head = 0;
  stream->rx_buf.tail = 0;
  stream->rx_buf.count = 0;
  pthread_mutex_unlock(&stream->rx_buf.lock);

  pthread_mutex_lock(&stream->tx_buf.lock);
  stream->tx_buf.head = 0;
  stream->tx_buf.tail = 0;
  stream->tx_buf.count = 0;
  pthread_mutex_unlock(&stream->tx_buf.lock);
}
