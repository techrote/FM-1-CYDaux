#ifndef CM_CONTROL7_H
#define CM_CONTROL7_H
#include "chrono.h"

/* Provisional instrument-local normalized knob semantics. Not MIDI CC IDs,
 * transport, a wire profile, or an ACK. Preset is a separate exact index.
 * Only the serialized audio owner may apply/read controls. */
typedef enum { CM_KNOB_BPM, CM_KNOB_WARP, CM_KNOB_VARIATION } cm_knob;
typedef enum { CM_KNOB_REJECT, CM_KNOB_UNCHANGED, CM_KNOB_CHANGED } cm_knob_result;
typedef struct { int applied; uint8_t canonical; } cm_knob_readback;

static inline int cm_knob_bounds(cm_knob knob, int *lo, unsigned *span) {
    switch (knob) {
    case CM_KNOB_BPM: *lo=40; *span=180; return 1;
    case CM_KNOB_WARP: *lo=-60; *span=120; return 1;
    case CM_KNOB_VARIATION: *lo=0; *span=100; return 1;
    default: return 0;
    }
}

/* Nearest integer, ties toward the larger value. BPM has 181 physical
 * values and only 128 codes, so not every local BPM is representable. */
static inline int cm_knob_decode(cm_knob knob, unsigned code, int *value) {
    int lo; unsigned span;
    if (code>127u || !cm_knob_bounds(knob,&lo,&span)) return 0;
    *value=lo+(int)((code*span+63u)/127u);
    return 1;
}
static inline int cm_knob_encode(cm_knob knob, int value, uint8_t *code) {
    int lo; unsigned span;
    if (!cm_knob_bounds(knob,&lo,&span) || value<lo || value>lo+(int)span) return 0;
    *code=(uint8_t)(((unsigned)(value-lo)*127u+span/2u)/span);
    return 1;
}
static inline int cm_knob_read(const cm_state *s, cm_knob knob, cm_knob_readback *out) {
    cm_knob_readback v={0};
    switch (knob) {
    case CM_KNOB_BPM: v.applied=s->bpm; break;
    case CM_KNOB_WARP: v.applied=s->warp; break;
    case CM_KNOB_VARIATION: v.applied=s->variation; break;
    default: return 0;
    }
    if (!cm_knob_encode(knob,v.applied,&v.canonical)) return 0;
    *out=v;
    return 1;
}

/* An unchanged quantized value is not an effective edit. Do not cancel a
 * score or reset phase on a rejected/no-op request. A changed value is a
 * manual edit: the enclosing owner must cancel active score automation
 * before the next render. No transport, origin, preset or pattern change. */
static inline cm_knob_result cm_knob_apply(cm_state *s, cm_knob knob, unsigned code) {
    int value; cm_knob_readback before;
    if (!cm_knob_decode(knob,code,&value) || !cm_knob_read(s,knob,&before)) return CM_KNOB_REJECT;
    if (value==before.applied) return CM_KNOB_UNCHANGED;
    unsigned bpm=s->bpm, variation=s->variation; int warp=s->warp;
    switch (knob) {
    case CM_KNOB_BPM: bpm=(unsigned)value; break;
    case CM_KNOB_WARP: warp=value; break;
    case CM_KNOB_VARIATION: variation=(unsigned)value; break;
    default: return CM_KNOB_REJECT;
    }
    return cm_controls(s,bpm,warp,variation)?CM_KNOB_CHANGED:CM_KNOB_REJECT;
}
#endif
