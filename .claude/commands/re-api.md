Studia come le app di riferimento usano il servizio ICAP indicato (nome di `struct icap_svc` o offset): $ARGUMENTS

L'ABI è quella ufficiale (`app/icap.h`, copia dell'SDK): questo comando serve a capire il
comportamento **non documentato** di un servizio, non a ricostruirne la firma.

1. Leggi la voce in `app/icap.h` e nel README di `ext/icap-sdk`.
2. Se esiste il sorgente, cercalo in `ext/icap-sdk/apps/` (attenzione alle licenze: le app
   GPL-3.0 si studiano, non si copiano in questo progetto MIT).
3. Per i binari: `python3 tools/icap_api_scan.py ext/apps/705/*.APP ext/apps/9700/*.APP` per i
   call-site, poi `icap_tool.py disasm APP --all` (~30 istruzioni attorno) per argomenti,
   valori di ritorno e gestione degli errori.
4. Riporta cosa hai trovato con le prove (app@indirizzo o file:riga). Se è comportamento non
   documentato, aggiungilo a `docs/ICAP_ABI.md` come ipotesi da verificare sulla radio.
Non modificare `app/icap.h`. Non indagare né usare i servizi TX (`tx_*`, `@TX`).
