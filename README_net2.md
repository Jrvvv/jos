# Сетевой стек JOS - Часть 2: ARP / ICMP / UDP / TCP / HTTP

## Содержание

1. [Обзор](#обзор)
2. [Архитектура](#архитектура)
3. [Компоненты стека](#компоненты-стека)
4. [Запуск](#запуск)
5. [Тестирование](#тестирование)
6. [Структура файлов](#структура-файлов)

---

## Обзор

Часть 2 реализует полноценный минимальный сетевой стек поверх драйвера e1000 из Части 1.

| Протокол | Поведение |
|----------|-----------|
| **ARP**  | Отвечает на ARP-запросы для своего IP, пассивно учит чужие MAC |
| **ICMP** | Отвечает на ping (Echo Request -> Echo Reply) |
| **UDP**  | Echo-сервер на портах 7 и 10001 |
| **TCP**  | Сервер на порту 80, принимает одно соединение |
| **HTTP** | Отдаёт статичную HTML-страницу по GET/HEAD запросу |

Сетевые параметры жёстко зашиты (задаются в QEMU):

```
IP   : 192.168.56.101
MAC  : 52:54:00:12:34:56
```

Тестирование ведётся напрямую по этому IP. `localhost` не работает - в данной
сборке QEMU нет SLIRP и проброса портов.

---

## Архитектура

### Где живёт стек

`sys_map_physical_region()` - системный вызов для отображения физической памяти
устройства - доступен **только** процессам с `env_type == ENV_TYPE_FS`. Именно
там живёт драйвер e1000.

Поэтому сетевой стек запускается **в том же процессе**, что и FS-сервер:

```
┌──────────────────────────────────────────────────────────┐
│  FS-сервер (fs/serv.c)  -  ENV_TYPE_FS                   │
│                                                          │
│  umain():                                                │
│    pci_init()   nvme_init()   e1000_init()               │
│    net_init()   <- инициализация TCP, вывод баннера      │
│    serve()      <- главный цикл                          │
│                                                          │
│  serve():                                                │
│    loop:                                                 │
│      sys_ipc_arm_recv()   <- вооружить IPC без блока     │
│      while (env_ipc_recving) net_poll()  <- опрос NIC    │
│      обработка FS IPC                                    │
└──────────────────────────────────────────────────────────┘
```

### Почему не отдельный процесс

`sys_exofork()` создаёт дочерний процесс с `ENV_TYPE_USER`, а значит он не может
вызвать `sys_map_physical_region()` - упадёт с `-E_BAD_ENV`. Поэтому сетевой стек
живёт в том же процессе, что и FS-сервер.

### Как работает polling без блокировки

Стандартный `ipc_recv()` блокирует вызывающий процесс (статус `ENV_NOT_RUNNABLE`).
Если все остальные процессы завершились, планировщик не найдёт ни одного
RUNNABLE-окружения и остановит систему ("Halt").

Чтобы этого избежать, добавлен новый системный вызов `SYS_ipc_arm_recv`:

```c
// kern/syscall.c
static int sys_ipc_arm_recv(uintptr_t dstva, uintptr_t maxsize) {
    curenv->env_ipc_dstva   = dstva;
    curenv->env_ipc_maxsz   = maxsize;
    curenv->env_ipc_recving = 1;
    return 0;   // не меняет статус, не вызывает sched_yield()
}
```

Вместо блокировки FS-сервер вооружает IPC-приёмник и крутится в цикле:

```c
sys_ipc_arm_recv(fsreq, PAGE_SIZE);
while (thisenv->env_ipc_recving)   // volatile-поле в UENVS
    net_poll();
// когда отправитель вызовет sys_ipc_try_send(), он увидит
// env_ipc_recving==1, запишет данные и сбросит флаг в 0
```

Результат:
- FS-сервер всегда RUNNABLE - система не останавливается
- `net_poll()` вызывается непрерывно - пакеты обрабатываются немедленно
- Когда приходит FS-запрос, флаг `env_ipc_recving` сбрасывается в 0 и цикл выходит

```
  входящий пакет
       |
       v
  [RX ring NIC]  <- буферизует до 16 фреймов
       |
       v                    FS-сервер крутится в цикле:
  net_poll() читает         while (env_ipc_recving) net_poll()
       |
       v
  eth_input() -> arp/ip -> icmp/udp/tcp -> http
```

---

## Компоненты стека

```
  ┌─────────────────────────────────────────┐
  │  HTTP  (fs/net_http.c)                  │  парсит GET/HEAD, отдаёт HTML
  ├─────────────────────────────────────────┤
  │  TCP   (fs/net_tcp.c)                   │  машина состояний, порт 80
  │  UDP   (fs/net_udp.c)                   │  echo-сервер, порты 7 / 10001
  │  ICMP  (fs/net_icmp.c)                  │  echo reply
  ├─────────────────────────────────────────┤
  │  IP    (fs/net_ip.c)                    │  чексумма, диспетчер по proto
  ├─────────────────────────────────────────┤
  │  ARP   (fs/net_arp.c)                   │  ответ на запросы, ARP-таблица
  │  ETH   (fs/net_eth.c)                   │  диспетчер по EtherType
  ├─────────────────────────────────────────┤
  │  e1000 (fs/e1000.c)                     │  TX/RX дескрипторные кольца
  └─────────────────────────────────────────┘
```

### MAC / Ethernet (`fs/net_eth.c`)

Разбирает 14-байтовый заголовок. По EtherType отправляет в ARP или IP.
При отправке добавляет заголовок, дополняет до минимума 60 байт.

### ARP (`fs/net_arp.c`)

Отвечает на ARP Request для нашего IP. Пассивно учит IP->MAC отправителя
в таблицу (16 записей). Самостоятельно ARP-запросы не шлёт.

### IP (`fs/net_ip.c`)

Проверяет версию и адрес назначения. Считает заголовочную чексумму.
По полю `proto` вызывает ICMP / UDP / TCP. Обновляет ARP-таблицу из
Ethernet-заголовка входящего пакета.

### ICMP (`fs/net_icmp.c`)

При получении Echo Request (тип 8) копирует id/seq/payload в Echo Reply
(тип 0), пересчитывает чексумму, отправляет обратно.

### UDP (`fs/net_udp.c`)

Echo-сервер: дейтаграммы на порты **7** (RFC 862) или **10001** отражаются
обратно. UDP-чексумма не считается (разрешено RFC 768).

### TCP (`fs/net_tcp.c`)

Машина состояний для одного соединения на порту **80**:

```
LISTEN --SYN--> SYN_RCVD --ACK--> ESTABLISHED
                                        |
                               данные/HTTP запрос
                                        |
                               tcp_close() --FIN--> FIN_WAIT_1
                                                        |
                                                   ACK + FIN от клиента
                                                        |
                                                   FIN_WAIT_2 -> LISTEN
```

При пассивном закрытии (FIN от клиента первым): ACK + FIN -> LAST_ACK -> LISTEN.

### HTTP (`fs/net_http.c`)

Ищет конец заголовков (`\r\n\r\n`). GET/HEAD - отвечает:

```
HTTP/1.0 200 OK
Content-Type: text/html; charset=utf-8
Content-Length: <N>
Connection: close

<html>...</html>
```

Затем вызывает `tcp_close()` для отправки FIN.

---

## Запуск

### Шаг 1 - собрать

```bash
make
```

### Шаг 2 - настроить TAP (один раз, требует sudo)

```bash
sudo ip tuntap add tap0 mode tap user $(whoami)
sudo ip link set tap0 up
sudo ip addr add 192.168.56.1/24 dev tap0
```

После этого хост видит сеть `192.168.56.0/24` через `tap0`,
JOS отвечает на `192.168.56.101`.

### Шаг 3 - запустить JOS

В одном терминале (QEMU занимает его):

```bash
make qemu-nox QEMUNET=tap TAP_NAME=tap0
```

Или в фоне (тесты в том же терминале):

```bash
make qemu-nox QEMUNET=tap TAP_NAME=tap0 </dev/null >/tmp/jos.log 2>&1 &
tail -f /tmp/jos.log
```

Ожидаемый вывод после загрузки:

```
e1000: MAC = 52:54:00:12:34:56
e1000: init done  link=up
net: stack ready  IP=192.168.56.101  MAC=52:54:00:12:34:56
net: HTTP on port 80, UDP echo on ports 7 and 10001
```

### Шаг 4 - убрать TAP после работы

```bash
sudo ip link delete tap0
```

---

## Тестирование

Все команды выполняются на **хосте** пока JOS запущен.
Подключаться нужно на `192.168.56.101` - не на `localhost`.

### ARP

```bash
arping -I tap0 192.168.56.101
```

```
ARPING 192.168.56.101 from 192.168.56.1 tap0
Unicast reply from 192.168.56.101 [52:54:00:12:34:56]  1.2ms
```

В консоли JOS:
```
net/arp: reply -> 192.168.56.1  MAC=8a:46:ac:4c:13:86
```

### ICMP (ping)

```bash
ping -c 3 192.168.56.101
```

```
64 bytes from 192.168.56.101: icmp_seq=1 ttl=64 time=2.04 ms
64 bytes from 192.168.56.101: icmp_seq=2 ttl=64 time=0.17 ms
64 bytes from 192.168.56.101: icmp_seq=3 ttl=64 time=0.22 ms
```

В консоли JOS:
```
net/icmp: echo reply -> 192.168.56.1  id=2 seq=1
net/icmp: echo reply -> 192.168.56.1  id=2 seq=2
net/icmp: echo reply -> 192.168.56.1  id=2 seq=3
```

### UDP echo

```bash
echo "Hello JOS" | nc -u -w 1 192.168.56.101 10001
```

```
Hello JOS
```

В консоли JOS:
```
net/udp: port 10001 <- 192.168.56.1:58386  10 bytes
net/udp: echo reply -> 192.168.56.1:58386
```

Стандартный echo-порт (RFC 862):
```bash
echo "test" | nc -u -w 1 192.168.56.101 7
```

### TCP / HTTP

```bash
curl http://192.168.56.101/
```

```html
<!DOCTYPE html>
<html>
<head><title>JOS Network Stack</title></head>
<body>
<h1>JOS Network Stack</h1>
...
```

В консоли JOS:
```
net/tcp: SYN from 192.168.56.1:40952
net/tcp: ESTABLISHED  192.168.56.1:40952
net/http: GET request received
net/http: response sent (99 hdr + 628 body bytes)
net/tcp: connection closed (FIN_WAIT_2)
```

Через браузер: `http://192.168.56.101/`

Через netcat (виден сырой TCP-обмен):
```bash
printf "GET / HTTP/1.0\r\nHost: 192.168.56.101\r\n\r\n" | nc 192.168.56.101 80
```

### Наблюдение трафика

```bash
sudo tshark -i tap0              # весь трафик
sudo tshark -i tap0 -f "arp"    # только ARP
sudo tshark -i tap0 -f "icmp"   # только ICMP
sudo tshark -i tap0 -f "tcp port 80"  # только HTTP
```

---

## Структура файлов

```
inc/
  syscall.h      - добавлен SYS_ipc_arm_recv

kern/
  syscall.c      - реализация sys_ipc_arm_recv()

lib/
  syscall.c      - user-space обёртка sys_ipc_arm_recv()
  lib.h          - объявление sys_ipc_arm_recv()

fs/
  net.h          - конфиг (IP, MAC), byte-order helpers, ARP API
  net.c          - net_init(), net_poll(), глобальный IP/MAC
  net_eth.h/c    - Ethernet: разбор/сборка заголовка
  net_arp.h/c    - ARP: ответ на Request, пассивная таблица
  net_ip.h/c     - IP: чексумма, ip_send(), диспетчер
  net_icmp.h/c   - ICMP: Echo Reply
  net_udp.h/c    - UDP: echo-сервер (порты 7 и 10001)
  net_tcp.h/c    - TCP: машина состояний, порт 80
  net_http.h/c   - HTTP/1.0: парсинг GET/HEAD, генерация ответа
  e1000.h/c      - драйвер Intel 82540EM
  serv.c         - serve(): sys_ipc_arm_recv + poll-loop вместо ipc_recv
```

### Поток вызовов при входящем пакете

```
net_poll() -> e1000_recv()
    \-- eth_input()
            +-- arp_input()          EtherType 0x0806
            |       \-- eth_send()   ARP Reply
            \-- ip_input()           EtherType 0x0800
                    +-- icmp_input() proto 1
                    |       \-- ip_send()  ICMP Echo Reply
                    +-- udp_input()  proto 17
                    |       \-- udp_send() Echo
                    \-- tcp_input()  proto 6
                            \-- http_process()
                                    +-- tcp_send_data()  HTTP response
                                    \-- tcp_close()      FIN
```
