Esegui la verifica completa del decoder CW e riassumi i risultati in italiano:

1. `make -C cwdec clean && make -C cwdec` (nessun warning)
2. `make -C cwdec test` e riporta la tabella CER; evidenzia ogni caso peggiorato rispetto a
   "Stato attuale" in CLAUDE.md
3. `make -C app clean && make -C app && make -C app check-sdk` (toolchain SDK, nessun warning,
   CRC OK in `icap_tool.py info`, icap.h identico all'SDK)
4. `make -C app sim`: entrambi i giri (normale e con buchi audio) devono dire "DECODE coincide
   con il testo atteso", senza VIOLAZIONE e con `should_stop` entro 500 ms
5. Se è stato modificato `cwdec/` o `app/`: `arm-none-eabi-nm app/build/cw.elf | grep aeabi`
   deve essere vuoto, e niente `double` nel percorso per campione (regola 6 di CLAUDE.md)

$ARGUMENTS
