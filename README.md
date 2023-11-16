# Linux chat

Program do wysyłania wiadomości tekstowych za pomocą protokołu TCP oraz socketów Unix. Projekt zawiera dwa pliki wykonywalne – serwer, który obsługuje zapytania od klientów i klient, który może wysyłać wiadomości do jednego lub wszystkich klientów.

Program działa na wątkach zaimplemetowanych za pomocą `pthreads`. Do jednoczesnej obsługi wielu socketów używany jest `epoll`.

## Kompilacja

```sh
make all
```

## Użycie

Uruchamianie serwera:
```sh
./server <numer portu TCP> <ścieżka socketu Unix>
```
Serwer nasłuchuje jednocześnie na porcie TCP i sockecie Unix.

Uruchamianie clienta:
```sh
./client <nazwa użytkownika> tcp <adres IP> <numer portu>
```
lub
```sh
./client <nazwa użytkownika> unix <ścieżka socketu>
```

Klient obsługuje następujące komendy podowane na standardowe wejście:

- `LIST` – wypisuje nazwy wszystkich pozostałych użytkowników
- `2ALL <wiadomość>` – wysyła wiadomość do wszytskich użytkowników
- `2ONE <nazwa użytkownika> <wiadomość>` – wysyła wiadomość do użytkownika podanego w parametrze
- `STOP` – kończy działanie klienta