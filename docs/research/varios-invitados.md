# Más de un invitado: qué lo impide hoy y por dónde se empieza

16-nov-2026, build del juego 1.0.0.3663. Repaso de solo lectura del código y de las notas (un subagente barrió el
repositorio; las referencias marcadas **[c]** las comprobé yo después, el resto son suyas). No se ejecutó nada ni se
cambió código. Las líneas son las de `main` de ese día (`2b26bba` y siguientes).

**Resumen.** El servidor del juego es lo que menos estorba sobre el papel: no comprueba el cupo de usuarios, no tiene
lista global de sesiones y reparte a «los que están cerca». Lo que es de un solo invitado es **el mod, en todas sus
capas por encima del `recv` del transporte**. La decisión D-33 ya fijó el aforo en 4 jugadores y dejó dicho que falta
generalizar «la sesión 2» a una tabla de sesiones (`opcion-d-diario.md:1803-1823` **[c]**). Nunca ha habido dos
usuarios invitados vivos a la vez (`pendientes/identidad.md:144`).

## 1. Dónde está el «uno»

### Transporte y túnel
- Steam (`ISteamNetworkingMessages`): el anfitrión con `accept_from=0` acepta la sesión de cualquier amigo que esté
  en el juego y `recv` devuelve el SteamID64 de quien manda (`net/steam_transport.cpp:462-473, 560-590`). UDP
  `recv_from` igual (`common/transport.h:68-73`). **El lado que escucha ya admite varios.**
- El corte está en `run_network` (`mod.cpp`): un `std::optional<PeerAddr> peer`, un `ClientSession`, un enlace
  `s2_link`; el anfitrión se queda con el primero que saluda y descarta a los demás (`mod.cpp:450-459` **[c]**).
  Hace falta una tabla `PeerAddr -> {ClientSession, TrocLink, ranura de invitado}`.
- `.ini` y launcher: `[steam] peer_steamid` es un solo valor en los dos papeles; el launcher guarda un `join_host` y
  un `host_accept` (`tools/launcher/launcher_logic.h:645-667, 719-720`). Con dos amigos en el juego, el invitado ya
  no puede «elegir al único amigo que juega» (`mod.cpp:420-431`): tiene que nombrar al anfitrión.

### Servidor, en el proceso del anfitrión
- La sesión se construye en `server.cpp:378-418` (0xD0 bytes con el asignador del juego, constructor de `NwSession`,
  vtable privada cuyo `Send` es del mod, nombre `'2'`). Todo lo demás es singular: `g_session2`, la época, la cola
  de salida `g_outgoing`, las claves `g_user2_userkey` / `g_user2_player`, el cierre de sesión, la cola de
  inyección, y la API entera (`server_user2*`, `server_live_session2`).
- Ya tiene forma de tabla: `g_epoch_session[33]`, un objeto de sesión por reconexión, encaminado por una etiqueta en
  el búfer del paquete (`server.cpp:122-126, 906-925`). Indexar por (ranura de invitado, época).
- El inicio de sesión es nativo (lo hacen las Req del cliente invitado). El mod reescribe tres cosas, las tres con
  un solo valor: la cuenta del 2490 (`[session2] account`, 1; tiene que ser distinta por usuario o el servidor toma
  la rama de reconexión), el characterNo del 3093 (constante `kUser2 = 0x7FFFFFFFFFFFF002`, `identity_fix.h:45-47`;
  las notas prevén F002/F003/F004) y la ranura de guardado (204).
- Las claves las da el servidor de su reserva (0x901… usuario, 0xA01… personaje): nada fija «índice 2».
- Lo que dicen las notas del servidor nativo: el cupo de usuarios (N=1; 10 en modo MMO) no se comprueba; cada
  usuario cuesta 101 claves de 15 501 (`opcion-d-d0.md:19-33, 84-86`); `ServerField::AddActor` limita el campo a un
  usuario y `field_share` lo salta metiendo `{usuario 2, 0}` en la lista del campo (`field_share.cpp:215-217,
  255-258`): eso también está escrito para uno.
- **El arreglo del relevo** (`server.cpp:1062-1088` **[c]**): `controlador == clave1 → excluye s1; == clave2 →
  excluye s2; si no, el original`, y el buscador original está vaciado: los Move / Action / Rotate de un tercer
  usuario no llegarían a nadie. Tiene que pasar a «clave del controlador → sesión».

### Encaminado de paquetes
- Salida: `take_frame(rq, self)` recibe la sesión pero empuja a la única `g_outgoing`; unas 25 llamadas a módulos
  van tras `self == live_session2()` (`server.cpp:443-685`). Entrada: `tunnel_on_packet → offer → server_inject_ex →
  una cola → bomba en el NetworkThread del anfitrión → búfer etiquetado → el gancho cambia la sesión`
  (`session2.cpp:1629-1686`). `from_user2` es un booleano que reciben cinco módulos.
- Por invitado harían falta: cola de salida, cola de inyección, cola de espera temprana (`g_early`) y ciclo de vida
  (`g_lc`).

### Módulos de `session2/`
- **Globales de un invitado, activos en el kit (lado anfitrión):** la familia `child_hold`, `observer_hold_acks`,
  `observer_scene_copy`, `observer_seq_copy`, `observer_milestone`, `user2_funnels`, `fast_travel`, `clock_gate`,
  `clock_push`, `identity_fix`, `field_share`, `housing_share`, `scene_level_share`, `horse_follow`, `attach_follow`,
  `combat_follow`, `mount_sync`, `focus_follow`, `weather_sync`, `gimmick_relay`, `guest_save_host/tunnel`, el ciclo
  de vida y la vuelta. Y en el cliente del anfitrión, `puppet_guard_owner` (una clave de dueño).
- **Ya con clave:** `cross_player_hit` (por usuario dueño), los pasajeros de `vehicle_follow` (`kMaxPlayers = 4`,
  por clave de personaje), el buzón de partidas del invitado (por SteamID64), `remote_interp` (por actor).
- **Simétricos o ajenos al número:** `save_hooks`, `host_save_export`, `report_guard`, `achievement_guard`,
  `ui_pause`, `ride_null_guard`, `create_retry` y los módulos del cliente invitado (`guest_seq_*`, `guest_sched_*`,
  `guest_x_*`, `guest_shop_*`).
- Los diez más difíciles, por orden: `child_hold` (5,6 mil líneas), `observer_hold_acks`, las copias de escena y
  secuencia con `observer_milestone`, `user2_funnels`, `guest_save_host/tunnel`, `identity_fix`, monturas
  (`mount_sync`, `horse_follow`, `ride_filter`: una pareja observada por cliente), ciclo de vida y vuelta,
  `housing_share` / `scene_level_share`, y `world_anchor` / `user2_stages` (el juego tiene **una** ranura nativa de
  «observador del mundo» por campo: solo un invitado podría mantener mundo lejos del anfitrión).

### Protocolo de mod a mod
- La cabecera no lleva quién manda (magia, versión, tipo, un u8 reservado, secuencia, hora de envío:
  `protocol.cpp:160-163`). `ClientSession` es un par: un `remote_state_`, un canal fiable, una época.
- Entre invitados: los personajes y los actores del mundo llegarían por el servidor en cuanto el relevo sea general.
  Por el canal del mod va lo que el servidor no reparte: posición en cubierta (`PlayerState`), `Riders`,
  `VehiclesTimed`, monturas, foco, velas, cartel y vida. El anfitrión tendría que repartirlo; las claves de actor
  que llevan valen en todos los clientes, así que basta un identificador de origen (el byte reservado, y subir
  `kVersion`).

### Partidas
- Una ranura (`kGuestSlot = 204`); las utilizables de 200-207 son 200, 202, 203 y 204 («la reserva de 4
  jugadores», `guest_save_logic.h:12-21`); solo la 204 se ha usado en vivo. Una carpeta `slot204.incoming`, un
  `g_guest_id`, una oferta a la vez en el túnel. El buzón de exportación ya es por invitado
  (`guest_saves\<SteamID64>\gen<n>`). El gestor de guardado del juego es único y tiene un byte de «ocupado»: los
  inicios de sesión y los guardados de dos invitados habría que hacerlos de uno en uno.

### Cliente invitado
- El panel tiene un `peer_name`, un estado remoto y un marcador. «El personaje del anfitrión» llega como estado de
  control 4 y es la única clave que `ride_filter` y `mount_sync` tratan como «el otro jinete».
  `vehicle_follow_on_peer_state` y `remote_motion_channel_position` reciben el estado de un par.

## 2. Lo que solo responde el juego en marcha

- Si un tercer usuario con su personaje inicia sesión de verdad (en estático el cupo no se comprueba; en vivo solo
  hay prueba con dos).
- Si la lista de usuarios del campo y `AddActor` aguantan tres entradas.
- Cómo lleva un cliente a dos usuarios ajenos (alias de dueño, caché de equipo por characterNo).
- Si el gestor de guardado aguanta dos invitados cargando o guardando, y si las ranuras 200/202/203 sirven.
- Dos cierres de sesión nativos a la vez, y la barrera con un tercer usuario activo.
- Subida y CPU del anfitrión: una unión son unos 42 000 Acks / 12 MB; D-33 estima 8-12 Mbit/s para cuatro.

## 3. Orden propuesto

1. **Tabla de pares** en `run_network`, con `ClientSession` y `TrocLink` por invitado, y el `.ini` / launcher
   (el anfitrión acepta a varios; el invitado nombra al anfitrión). Se prueba sin juego: tres `cdmp_host.exe`
   cargando `cdmp.asi` desde tres carpetas de `build\e2e\` (un `role=host transport=udp` y dos invitados).
   `cdmp_peer` no sirve tal cual: es siempre el que responde y releva dos clientes como mucho.
2. **`server.cpp` con tabla de ranuras de invitado**: sesiones, colas, claves, fase y cierre por ranura; etiqueta =
   ranura + época; cuenta y characterNo por ranura; relevo general; `field_share` para cualquier invitado. Primera
   prueba con UN solo juego: repetir la antigua etapa E2 para un usuario 3 (dos sesiones extra, solo el inicio de
   sesión de usuario con cuentas 1 y 2). Pasa si hay tres `ServerUserActor`, claves 0x901 distintas, 101 claves
   menos por usuario, cierres limpios y `ERR` 0. Hace falta portar la repetición de capturas a 3663 o un inyector
   dentro del mod.
3. Ciclo de vida y barrera por invitado. 4. Partidas por ranura, con los inicios de sesión de uno en uno.
5. **Unión y movimiento de dos invitados** con los módulos de funciones apagados para el segundo. Con dos clientes
   de verdad: las dos copias de este PC por loopback más el segundo PC por UDP en la red local
   (`host_loopback_only=0`). Tres copias en un PC no valen hoy: los papeles son anfitrión | invitado, un segundo
   proceso con papel de invitado se queda inerte y los dos compartirían `save_cdmp_guest` (`role_logic.h:8-10,
   22-25`).
6. Reparto por el canal del mod con identificador de origen, y el panel para varios.
7. Los módulos, de uno en uno en el orden de arriba, cada uno tras su interruptor del `.ini`.

Los pasos 1 y 2 no cambian nada para dos jugadores si la tabla tiene una entrada; son los que se pueden escribir y
probar sin un tercer cliente. El paso 5 es el primero que un jugador notaría y necesita el segundo PC.
