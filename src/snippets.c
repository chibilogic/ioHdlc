bool isDuplicateSupervision(const peer_t* pr, uint8_t s_code, uint8_t nr, bool busy_now)
{
    if (!pr->sup.last_valid) return false;

    // RR vs RNR: puoi anche derivare busy_now da s_code
    bool same = (pr->sup.last_s_code == s_code) &&
                (pr->sup.last_nr     == nr)     &&
                (pr->sup.last_busy   == busy_now);

    if (!same) return false;

    // opzionale: debounce temporale
    // if (tick_now() - pr->sup.last_tx_ts < pr->sup.debounce_ms) return true;

    return true;
}

// Call dopo invio S:
void markSupervisionSent(peer_t* pr, uint8_t s_code, uint8_t nr, bool busy_now) {
    pr->sup.last_s_code = s_code;
    pr->sup.last_nr     = nr;
    pr->sup.last_busy   = busy_now;
    pr->sup.last_valid  = true;
    pr->sup.last_tx_ts  = tick_now(); // se usi debounce
}

bool isRetransmissionOfCurrentPoll(const station_t* st, const peer_t* pr,
                                   uint8_t s_code, bool wantP)
{
    // Deve essere esattamente lo stesso peer e stesso "tipo" di poll
    if (!st->poll.active)                   return false;
    if (st->poll.peer != pr)                return false;
    if (!wantP)                             return false;  // retry è sempre P=1
    if (st->poll.s_code != s_code)          return false;  // stesso S-code (es. RR)

    // Timer e retries
    if (!t200_expired(&st->poll))           return false;
    if (st->poll.retries >= st->poll.max_retries) return false;

    return true;
}

// Call dopo invio retry:
void markPollRetransmitted(station_t* st) {
    st->poll.retries++;
    restart_t200(&st->poll);
}


// === Tipi e helper attesi ===============================================

typedef enum { HDLC_FK_U, HDLC_FK_S, HDLC_FK_I } hdlc_frame_kind_t;
typedef enum { S_RR, S_RNR, S_REJ, S_SREJ } s_code_t;

typedef struct { /* opzionale per U */ } u_intent_t;

typedef struct {                  // Intent per S-frame
  s_code_t s_code;                // RR/RNR/REJ/...
  uint8_t  nr;                    // N(R) che intendi trasmettere
  bool     request_P;             // vuoi P=1 (sonda/retry-poll)? (deciso dal core)
} s_intent_t;

typedef struct {                  // Intent per I-frame
  uint8_t  ns;                    // N(S) che intendi usare
  bool     request_P;             // vuoi P=1 (sollecito)? (deciso dal core)
} i_intent_t;

typedef struct {
  hdlc_frame_kind_t kind;
  union { u_intent_t u; s_intent_t s; i_intent_t i; };
} frame_intent_t;

typedef enum {
  GATE_OK = 0,
  GATE_NO_LINK, GATE_NO_IDLE, GATE_WRONG_ROLE,
  GATE_BUSY_PEER, GATE_WIN_FULL, GATE_POLL_OPEN,
  GATE_DUP_SUPERVISION
} gating_cause_t;

typedef struct {
  bool allowed;   // posso inviare ORA questo intent?
  bool set_P;     // P suggerito (puoi ignorarlo se hai già deciso nell'intent)
  bool set_F;     // F suggerito (tipicamente risposte a poll)
  gating_cause_t cause;
} gating_result_t;

#define ALLOW(P,F) ((gating_result_t){true,(P),(F),GATE_OK})
#define DENY(C)    ((gating_result_t){false,false,false,(C)})

// Helper esterni (implementati altrove)
bool      twa_required(const station_t* st);
bool      poll_open(const station_t* st, const peer_t* pr);
bool      poll_pending(const station_t* st, const peer_t* pr);
bool      peerBusy(const peer_t* pr);
bool      txWindowAvailable(const station_t* st, const peer_t* pr);
bool      isDuplicateSupervision(const peer_t* pr, s_code_t s, uint8_t nr, bool busy_now);
bool      isRetransmissionOfCurrentPoll(const station_t* st, const peer_t* pr, s_code_t s);

// Stato atteso in station/peer (ampiamente discusso in chat):
// - st->mode ∈ {IOHDLC_OM_NRM, IOHDLC_OM_ARM, IOHDLC_OM_ABM}
// - st->role ∈ {ROLE_PRIMARY, ROLE_SECONDARY, ROLE_COMBINED}
// - st->idle (per TWA), st->current_poll_peer, st->current_arm_peer
// - pr->… (busy, finestra, ecc.)
typedef enum { HDLC_FK_U, HDLC_FK_S, HDLC_FK_I } hdlc_frame_kind_t;
typedef enum { S_RR, S_RNR, S_REJ, S_SREJ } s_code_t;

typedef struct { /* opzionale per U */ } u_intent_t;

typedef struct {
  s_code_t s_code;    // RR/RNR/REJ/...
  uint8_t  nr;        // N(R) proposto
  bool     request_P; // vuoi P=1? (sonda/retry poll) — deciso dal core
  bool     response_final; // ✅ questo S chiude la risposta al poll?
} s_intent_t;

typedef struct {
  uint8_t ns;         // N(S) proposto
  bool    request_P;  // vuoi P=1? (sollecito) — deciso dal core
  bool    response_final; // ✅ questo I chiude la risposta al poll?
} i_intent_t;

typedef struct {
  hdlc_frame_kind_t kind;
  const peer_t*     peer;
  union { u_intent_t u; s_intent_t s; i_intent_t i; };
} frame_intent_t;

gating_result_t canSendFrame(const station_t* st,
                             const peer_t*    pr,
                             const frame_intent_t* it)
{
    if (!st->link_up) return DENY(GATE_NO_LINK);

    // TWA: serve idle per INIZIARE TX (non blocca risposta multi-frame del secondario)
    if (twa_required(st) && !st->idle) return DENY(GATE_NO_IDLE);

    // I-frame: flow control
    if (it->kind == HDLC_FK_I) {
        if (peerBusy(pr))               return DENY(GATE_BUSY_PEER);
        if (!txWindowAvailable(st, pr)) return DENY(GATE_WIN_FULL);
    }

    switch (st->mode) {

    // ============================= NRM =============================
    case IOHDLC_OM_NRM: {
        if (st->role == ROLE_PRIMARY) {

            if (poll_open(st, pr)) {
                if (pr != st->current_poll_peer) return DENY(GATE_POLL_OPEN);

                if (twa_required(st)) {
                    // TWA: solo retry dello STESSO S(P=1)
                    if (it->kind != HDLC_FK_S)         return DENY(GATE_POLL_OPEN);
                    if (!it->s.request_P)              return DENY(GATE_POLL_OPEN);
                    if (!isRetransmissionOfCurrentPoll(st, pr, it->s.s_code))
                                                      return DENY(GATE_POLL_OPEN);
                    return ALLOW(true,false);
                } else {
                    // TWS: S(P=0) verso lo stesso peer ammessi; I no
                    if (it->kind == HDLC_FK_S) {
                        if (it->s.request_P && isRetransmissionOfCurrentPoll(st, pr, it->s.s_code))
                            return ALLOW(true,false);
                        const bool busy_now = (it->s.s_code == S_RNR);
                        if (isDuplicateSupervision(pr, it->s.s_code, it->s.nr, busy_now))
                            return DENY(GATE_DUP_SUPERVISION);
                        return ALLOW(false,false); // RR/RNR/REJ(P=0)
                    }
                    return DENY(GATE_POLL_OPEN); // I durante poll aperto: evita
                }
            }

            // Nessun poll aperto
            if (it->kind == HDLC_FK_S) {
                if (it->s.request_P) return ALLOW(true,false); // RR(P=1) deciso dal core
                const bool busy_now = (it->s.s_code == S_RNR);
                if (isDuplicateSupervision(pr, it->s.s_code, it->s.nr, busy_now))
                    return DENY(GATE_DUP_SUPERVISION);
                return ALLOW(false,false);
            }
            if (it->kind == HDLC_FK_I)
                return ALLOW(it->i.request_P, false);
            return ALLOW(false,false);
        }
        else { // ===== SECONDARY in NRM =====
            if (!poll_pending(st, pr)) return DENY(GATE_WRONG_ROLE);

            // ✅ Risposta multi-frame consentita: S/I con F=0 ripetuti, poi ultimo con F=1
            if (it->kind == HDLC_FK_S) {
                return ALLOW(/*P=*/false, /*F=*/it->s.response_final);
            }
            if (it->kind == HDLC_FK_I) {
                return ALLOW(/*P=*/it->i.request_P, /*F=*/it->i.response_final);
            }
            // U di risposta (UA/DM/…): chiude sempre
            return ALLOW(false, /*F=*/true);
        }
    }

    // ============================= ARM =============================
    case IOHDLC_OM_ARM: {
        if (st->role == ROLE_PRIMARY) {
            if (poll_open(st, pr)) {
                if (pr != st->current_poll_peer) return DENY(GATE_POLL_OPEN);

                if (twa_required(st)) {
                    if (it->kind != HDLC_FK_S)         return DENY(GATE_POLL_OPEN);
                    if (!it->s.request_P)              return DENY(GATE_POLL_OPEN);
                    if (!isRetransmissionOfCurrentPoll(st, pr, it->s.s_code))
                                                      return DENY(GATE_POLL_OPEN);
                    return ALLOW(true,false);
                } else {
                    if (it->kind == HDLC_FK_S) {
                        if (it->s.request_P && isRetransmissionOfCurrentPoll(st, pr, it->s.s_code))
                            return ALLOW(true,false);
                        const bool busy_now = (it->s.s_code == S_RNR);
                        if (isDuplicateSupervision(pr, it->s.s_code, it->s.nr, busy_now))
                            return DENY(GATE_DUP_SUPERVISION);
                        return ALLOW(false,false);
                    }
                    return DENY(GATE_POLL_OPEN);
                }
            }

            if (it->kind == HDLC_FK_S) {
                if (it->s.request_P) return ALLOW(true,false);
                const bool busy_now = (it->s.s_code == S_RNR);
                if (isDuplicateSupervision(pr, it->s.s_code, it->s.nr, busy_now))
                    return DENY(GATE_DUP_SUPERVISION);
                return ALLOW(false,false);
            }
            if (it->kind == HDLC_FK_I) return ALLOW(it->i.request_P, false);
            return ALLOW(false,false);
        } else { // SECONDARY in ARM
            if (pr != st->current_arm_peer) {
                if (!poll_pending(st, pr)) return DENY(GATE_WRONG_ROLE);
            }

            if (poll_pending(st, pr)) {
                // Anche qui, come in NRM, consenti multi-frame: F sull'ultimo
                if (it->kind == HDLC_FK_S) return ALLOW(false, it->s.response_final);
                if (it->kind == HDLC_FK_I) return ALLOW(it->i.request_P, it->i.response_final);
                return ALLOW(false,true);
            }

            // iniziativa del secondary ARM attivo
            if (it->kind == HDLC_FK_S) {
                const bool busy_now = (it->s.s_code == S_RNR);
                if (isDuplicateSupervision(pr, it->s.s_code, it->s.nr, busy_now))
                    return DENY(GATE_DUP_SUPERVISION);
                return ALLOW(false,false);
            }
            if (it->kind == HDLC_FK_I) return ALLOW(it->i.request_P, false);
            return ALLOW(false,false);
        }
    }

    // ============================= ABM =============================
    case IOHDLC_OM_ABM: {
        if (poll_pending(st, pr)) {
            // In ABM puoi anche fare risposte multi-frame; F sull'ultimo se vuoi “chiudere”
            if (it->kind == HDLC_FK_S) return ALLOW(false, it->s.response_final);
            if (it->kind == HDLC_FK_I) return ALLOW(it->i.request_P, it->i.response_final);
            return ALLOW(false,true);
        } else {
            if (it->kind == HDLC_FK_S) {
                const bool busy_now = (it->s.s_code == S_RNR);
                if (!it->s.request_P && isDuplicateSupervision(pr, it->s.s_code, it->s.nr, busy_now))
                    return DENY(GATE_DUP_SUPERVISION);
                return ALLOW(it->s.request_P, false);
            }
            if (it->kind == HDLC_FK_I) return ALLOW(it->i.request_P, false);
            return ALLOW(false,false);
        }
    }

    default:
        return ALLOW(false,false);
    }
}

// === canSendFrame ========================================================

gating_result_t canSendFrame(const station_t* st,
                             const peer_t*    pr,
                             const frame_intent_t* it)
{
    // A) controlli generali
    if (!st->link_up) return DENY(GATE_NO_LINK);

    // TWA: serve idle per INIZIARE una TX
    if (twa_required(st) && !st->idle) return DENY(GATE_NO_IDLE);

    // B) vincoli comuni per I-frame
    if (it->kind == HDLC_FK_I) {
        if (peerBusy(pr))               return DENY(GATE_BUSY_PEER);
        if (!txWindowAvailable(st, pr)) return DENY(GATE_WIN_FULL);
    }

    // C) per-modalità
    switch (st->mode) {

    // ============================= NRM =============================
    case IOHDLC_OM_NRM: {
        if (st->role == ROLE_PRIMARY) {

            if (poll_open(st, pr)) {
                // bloccato sullo stesso peer finché non chiudi F o timeout
                if (pr != st->current_poll_peer)
                    return DENY(GATE_POLL_OPEN);

                if (twa_required(st)) {
                    // TWA: unica TX ammessa è retry dello STESSO S(P=1)
                    if (it->kind != HDLC_FK_S)                    return DENY(GATE_POLL_OPEN);
                    if (!it->s.request_P)                         return DENY(GATE_POLL_OPEN);
                    if (!isRetransmissionOfCurrentPoll(st, pr, it->s.s_code))
                                                                  return DENY(GATE_POLL_OPEN);
                    return ALLOW(/*P=*/true, /*F=*/false);
                } else { // TWS
                    if (it->kind == HDLC_FK_S) {
                        // retry del poll corrente?
                        if (it->s.request_P && isRetransmissionOfCurrentPoll(st, pr, it->s.s_code))
                            return ALLOW(true,false);

                        // S di ack/flow-control con P=0 consentiti
                        const bool busy_now = (it->s.s_code == S_RNR);
                        if (isDuplicateSupervision(pr, it->s.s_code, it->s.nr, busy_now))
                            return DENY(GATE_DUP_SUPERVISION);
                        return ALLOW(false,false); // RR/RNR/REJ(P=0)
                    }
                    // I durante poll aperto: in NRM classico evitati
                    return DENY(GATE_POLL_OPEN);
                }
            }

            // Nessun poll aperto: il core decide se proporre sonda (RR(P=1)) o altro.
            if (it->kind == HDLC_FK_S) {
                if (it->s.request_P) {
                    // apri poll verso questo peer
                    return ALLOW(true,false);
                } else {
                    // S(P=0) di ack/flow-control
                    const bool busy_now = (it->s.s_code == S_RNR);
                    if (isDuplicateSupervision(pr, it->s.s_code, it->s.nr, busy_now))
                        return DENY(GATE_DUP_SUPERVISION);
                    return ALLOW(false,false);
                }
            }
            if (it->kind == HDLC_FK_I) {
                // opzionale: molti NRM inviano I solo dentro cicli di polling
                return ALLOW(/*P=*/it->i.request_P, /*F=*/false);
            }
            // U gestiti altrove come priorità massima
            return ALLOW(false,false);
        }
        else { // SECONDARY in NRM
            // Parla solo se c'è un Poll pendente (o U di risposta)
            if (!poll_pending(st, pr)) return DENY(GATE_WRONG_ROLE);

            // Risposta a P=1: F suggerito
            if (it->kind == HDLC_FK_S) return ALLOW(false,true);
            if (it->kind == HDLC_FK_I) return ALLOW(it->i.request_P, true);
            // U di risposta (UA/DM/…): builder esterno imposterà F=1
            return ALLOW(false,true);
        }
    }

    // ============================= ARM =============================
    case IOHDLC_OM_ARM: {
        if (st->role == ROLE_PRIMARY) {
            // come NRM per la disciplina del poll
            if (poll_open(st, pr)) {
                if (pr != st->current_poll_peer) return DENY(GATE_POLL_OPEN);

                if (twa_required(st)) {
                    if (it->kind != HDLC_FK_S)                    return DENY(GATE_POLL_OPEN);
                    if (!it->s.request_P)                         return DENY(GATE_POLL_OPEN);
                    if (!isRetransmissionOfCurrentPoll(st, pr, it->s.s_code))
                                                                  return DENY(GATE_POLL_OPEN);
                    return ALLOW(true,false);
                } else { // TWS
                    if (it->kind == HDLC_FK_S) {
                        if (it->s.request_P && isRetransmissionOfCurrentPoll(st, pr, it->s.s_code))
                            return ALLOW(true,false);
                        const bool busy_now = (it->s.s_code == S_RNR);
                        if (isDuplicateSupervision(pr, it->s.s_code, it->s.nr, busy_now))
                            return DENY(GATE_DUP_SUPERVISION);
                        return ALLOW(false,false);
                    }
                    return DENY(GATE_POLL_OPEN); // I durante poll aperto: evita
                }
            }

            // senza poll aperto
            if (it->kind == HDLC_FK_S) {
                if (it->s.request_P) return ALLOW(true,false); // RR(P=1) deciso dal core
                const bool busy_now = (it->s.s_code == S_RNR);
                if (isDuplicateSupervision(pr, it->s.s_code, it->s.nr, busy_now))
                    return DENY(GATE_DUP_SUPERVISION);
                return ALLOW(false,false);
            }
            if (it->kind == HDLC_FK_I) return ALLOW(it->i.request_P, false);
            return ALLOW(false,false);
        } else { // SECONDARY in ARM
            // solo un secondary ARM attivo può iniziare TX spontanee
            if (pr != st->current_arm_peer) {
                // può comunque rispondere a un poll pendente
                if (!poll_pending(st, pr)) return DENY(GATE_WRONG_ROLE);
            }

            if (poll_pending(st, pr)) {
                // risposta a P=1
                if (it->kind == HDLC_FK_S) return ALLOW(false,true);
                if (it->kind == HDLC_FK_I) return ALLOW(it->i.request_P, true);
                return ALLOW(false,true);
            }

            // iniziativa del secondary ARM attivo
            if (it->kind == HDLC_FK_S) {
                const bool busy_now = (it->s.s_code == S_RNR);
                if (isDuplicateSupervision(pr, it->s.s_code, it->s.nr, busy_now))
                    return DENY(GATE_DUP_SUPERVISION);
                // lato secondary, P=1 raro: tienilo a false salvo policy
                return ALLOW(false,false);
            }
            if (it->kind == HDLC_FK_I) return ALLOW(it->i.request_P, false);
            return ALLOW(false,false);
        }
    }

    // ============================= ABM =============================
    case IOHDLC_OM_ABM: {
        // combined: entrambe possono comandare/rispondere
        if (poll_pending(st, pr)) {
            // stai rispondendo a P=1 → chiudi col F (checkpoint gestito a RX)
            if (it->kind == HDLC_FK_S) return ALLOW(false,true);
            if (it->kind == HDLC_FK_I) return ALLOW(it->i.request_P, true);
            return ALLOW(false,true); // U di risposta
        } else {
            // iniziativa
            if (it->kind == HDLC_FK_S) {
                // se il core ha deciso una sonda, request_P è già true
                const bool busy_now = (it->s.s_code == S_RNR);
                if (!it->s.request_P && isDuplicateSupervision(pr, it->s.s_code, it->s.nr, busy_now))
                    return DENY(GATE_DUP_SUPERVISION);
                return ALLOW(it->s.request_P, false);
            }
            if (it->kind == HDLC_FK_I) return ALLOW(it->i.request_P, false);
            return ALLOW(false,false);
        }
    }

    default:
        return ALLOW(false,false);
    }
}
