/* M2.0 voice bring-up probe.
 *
 * Answers exactly one question: can this SIM place a real voice call?
 * Everything else about the phone (UI, SMS, storage) is wasted effort if the
 * answer is no, so this runs before any UI work.
 *
 * Flow: wait for the AT client from at_probe, register the ^DSCI call-state
 * URC, dump registration/time/VoLTE state, dial, observe for 12 s, hang up.
 * A failed/uncertain dial result also enters hangup cleanup. Disabled by default.
 */
#ifndef PHONE_PROBE_H
#define PHONE_PROBE_H

void phone_probe_start(void);

#endif /* PHONE_PROBE_H */
