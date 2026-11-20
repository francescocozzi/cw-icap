# Messaggio per SQ6EMM

Il primo messaggio (richiesta dell'header ABI) è superato: il 2026-11-20 è uscito l'SDK ufficiale
(https://github.com/sq6emm/icap-sdk). Restano solo domande che l'SDK non copre. Da mandare
**dopo** la prova di PROBE.APP sulla radio (fase 2), così le domande si riducono a ciò che la
prova non chiarisce. Bozza in inglese.

---

**Subject:** CW decoder app on the ICAP SDK — two questions about decoder modes

Hi Dawid,

thanks for publishing the ICAP SDK. I've ported my CW (Morse) decoder app to it: RX only, it
uses `icap_audio_read`, `icap_decode_out` and `icap_set_line`, and it is packed with
`--background --mode CW:keep`. I'd like to offer it for the bundle under MIT.

Three things I couldn't find in the SDK:

1. **`ICAP_RMODE_KEEP` and the RTTY button cycle.** Tested on IC-705 E, 20261119v14: with `keep`
   the radio stays in RTTY-R (the last mode of the cycle) and the DECODE screen opens as
   "CW DECODE". Pressing CW afterwards keeps the app running and the radio is back in CW, so it
   works with one extra key press. Would it be possible for `keep` to restore the mode the radio
   was in before the cycle started? In RTTY-R the audio is in the RTTY passband and a CW decoder
   hears nothing.
2. **Audio in CW mode.** Is `icap_audio_read` taken after the CW IF filter (so a narrow filter
   helps the decoder), and is it independent of the AF gain?
3. **Apps menu limit.** On IC-705 E with firmware and SD apps 20261119v14, MENU > SET > Apps shows
   only 8 entries. With the full bundle (8 apps) a ninth `PROBE.APP`, header identical in layout
   to `CLOCK.APP`, was not listed; after removing `PI4.APP` it appeared. Is 8 the intended
   limit? Is it documented? Could it be raised (or the list scroll), or should third-party
   apps expect users to drop a bundle app?

Results from the radio so far: [riempire dopo la fase 2].

73, Francesco IZ7KHR
