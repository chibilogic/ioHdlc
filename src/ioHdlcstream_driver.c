/*
 * HDLC driver implemented on top of ioHdlcStream core.
 * Expects an already initialized ioHdlcStreamPort from the HAL adapter.
 */

#include "ioHdlcdriver.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"
#include "ioHdlcqueue.h"
#include "ioHdlcll.h"

#include "ioHdlcstream.h"
#include "ioHdlcstream_driver.h"

/* VMT forward */
static size_t drv_send_frame(void *instance, iohdlc_frame_t *fp);
static iohdlc_frame_t *drv_recv_frame(void *instance, iohdlc_timeout_t tmo);
static void drv_start(void *instance, void *phyp, void *phyconfigp, ioHdlcFramePool *fpp);
static bool drv_get_hwtransparency(void *instance);
static void drv_set_applytransparency(void *instance, bool tr);
static void drv_set_hasframeformat(void *instance, bool hff);

static const struct _iohdlc_driver_vmt s_vmt = {
  .start                 = drv_start,
  .send_frame            = drv_send_frame,
  .recv_frame            = drv_recv_frame,
  .get_hwtransparency    = drv_get_hwtransparency,
  .set_applytransparency = drv_set_applytransparency,
  .set_hasframeformat    = drv_set_hasframeformat
};

/* Core -> upper delivery callback (ISR). */
static void s_core_deliver_rx(void *upper_ctx, void *framep, size_t len) {
  ioHdclStreamDriver *ip = (ioHdclStreamDriver *)upper_ctx;

  /* framep == NULL -> line idle.*/
  if (framep) {
    iohdlc_frame_t *fp = (iohdlc_frame_t *)framep;
    fp->elen = (uint16_t)len;
    ioHdlc_frameq_insert(&ip->raw_recept_q, fp);
  }
  iohdlc_sys_lock_isr();
  iohdlc_sem_signal_i(&ip->raw_recept_sem);
  iohdlc_sys_unlock_isr();
}

void ioHdclStreamDriverInit(ioHdclStreamDriver *uhp) {
  uhp->vmt = &s_vmt;
  uhp->fpp = NULL;
  uhp->apply_transparency = false;
  uhp->has_frame_format = false;
  ioHdlc_frameq_init(&uhp->raw_recept_q);
  iohdlc_sem_init(&uhp->raw_recept_sem, 0);
}

static void drv_start(void *instance, void *phyp, void *phyconfigp, ioHdlcFramePool *fpp) {
  (void)phyconfigp;
  ioHdclStreamDriver *ip = (ioHdclStreamDriver *)instance;
  ioHdlcStreamPort *portp = (ioHdlcStreamPort *)phyp;

  ip->fpp = fpp;

  ioHdlcStreamConfig cfg = {
    .has_frame_format   = ip->has_frame_format,
    .apply_transparency = ip->apply_transparency,
    .deliver_rx_frame   = s_core_deliver_rx,
  };

  (void)ioHdlcStream_init(&ip->core, portp, &cfg, ip->fpp, ip);
  (void)ioHdlcStream_start(&ip->core);
}

static size_t drv_send_frame(void *instance, iohdlc_frame_t *fp) {
  ioHdclStreamDriver *ip = (ioHdclStreamDriver *)instance;
  iohdlc_frame_t *nfp = fp;
  size_t size = 0;

  if (fp->elen) {
    frameAddFCS(fp);
    size = fp->elen;
    if (ip->apply_transparency) {
      nfp = hdlcTakeFrame(ip->fpp);
      if (nfp == NULL)
        return 1; /* NOMEM */
      (void)frameTransparentEncode(nfp, fp);
      size = nfp->elen;
    } else {
      hdlcAddRef(ip->fpp, nfp);
    }
  }

  nfp->frame[size] = HDLC_FLAG; /* closing flag */
  nfp->openingflag = 0;
  if (ip->core.port.ops && ip->core.port.ops->tx_busy &&
      !ip->core.port.ops->tx_busy(ip->core.port.ctx)) {
    nfp->openingflag = HDLC_FLAG;
    ++size;
  }

  const uint8_t *ptr = nfp->openingflag ? &nfp->openingflag : nfp->frame;
  size_t len = size + 1; /* include closing flag */

  while (!ioHdlcStream_send(&ip->core, ptr, len, (void *)nfp)) {
    iohdlc_thread_yield();
  }
  return 0;
}

static iohdlc_frame_t *drv_recv_frame(void *instance, iohdlc_timeout_t tmo) {
  ioHdclStreamDriver *ip = (ioHdclStreamDriver *)instance;
  iohdlc_frame_t *fp;

  for (;;) {
    fp = NULL;
    if (iohdlc_sem_wait_ok(&ip->raw_recept_sem, tmo)) {
      iohdlc_sys_lock();
      if (!ioHdlc_frameq_isempty(&ip->raw_recept_q))
        fp = ioHdlc_frameq_remove(&ip->raw_recept_q);
      iohdlc_sys_unlock();
    }
    if (fp == NULL)
      return NULL; /* timeout or idle */

    if (ip->apply_transparency)
      frameTransparentDecode(fp, fp);

    if (frameCheckFCS(fp) && (!ip->has_frame_format || (fp->elen == fp->frame[0])))
      break;

    hdlcReleaseFrame(ip->fpp, fp);
  }

  fp->elen -= 2; /* drop FCS */
  return fp;
}

static bool drv_get_hwtransparency(void *instance) {
  (void)instance;
  return false;
}

static void drv_set_applytransparency(void *instance, bool tr) {
  ioHdclStreamDriver *ip = (ioHdclStreamDriver *)instance;
  ip->apply_transparency = tr;
  ip->core.cfg.apply_transparency = tr;
}

static void drv_set_hasframeformat(void *instance, bool hff) {
  ioHdclStreamDriver *ip = (ioHdclStreamDriver *)instance;
  ip->has_frame_format = hff;
  ip->core.cfg.has_frame_format = hff;
}
