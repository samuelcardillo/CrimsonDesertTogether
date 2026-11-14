# Herramientas del barco (13-nov-2026, build 1.0.0.3576)

Diagnóstico del barco en cooperativo. Ver `docs/ESTADO-3527.md`, entrada «13-nov: BARCO, DIAGNÓSTICO».

- `shipscan.py captura.bin clave [desde_GB] [hasta_GB]`: `MoveActorReq` (2833) y `MoveActorAck` (3094) de una clave,
  por regex sobre la captura (segundos con capturas de varios GB). Pose de mundo en `+196`, sello en `+212`.
- `keyops.py captura.bin clave t_desde t_hasta`: todos los mensajes que nombran la clave en una ventana, por opcode.
- `opcount.py captura.bin t_desde t_hasta [ops]`: recuento por (dirección, opcode) en una ventana.
- `shipmem.py pid clave`, `comps.py pid clave`, `dumpobj.py pid dir tam`: lectura (solo lectura) del actor, su
  TransformSync y sus componentes con nombres RTTI (`tools/porting/data/rtti_3576.json`). Mundo en `exe+0x71C0100`.
- `hw_watch_ship.cpp`: DLL de diagnóstico. Punto de ruptura hardware de escritura (DR0) sobre la posición del barco en
  todos los hilos y un VEH; registra el sitio que escribe y, si la posición salta más de 30 m, la pila desenrollada. No
  llama a nada del juego; se quita sola (al cuarto salto, a los 50 min o con `shiptrace.stop` junto a la DLL). Se
  compila con `cl /LD /O2 /EHsc /std:c++20` y se carga en la copia del invitado con `inject.py pid ruta.dll`. Solo para
  pruebas en local; haz copia de las partidas antes.
- `hw_watch.cpp`: la misma DLL, genérica: `<dll>.cfg` con `dirección longitud sites|jump|req [x z sector]`; log
  compartido (se lee mientras corre). Con ella se encontraron el escritor por fotograma y la petición de tipo 7.
- `vehicle_follow_test.cpp`: el experimento que dio el arreglo (puntos de ruptura de EJECUCIÓN en los dos setters;
  cambia la pose de la petición de tipo 7 del vehículo por su pose de red). Ya no hace falta: es `session2/vehicle_follow`.
- `vfmon.py host guest clave segundos`: la pose del barco en las dos copias cada 0,5 s y la distancia.
  `vfmon3.py`: además el personaje del anfitrión respecto al barco (en el anfitrión su TS es local: no sirve allí).
- `vfreplay.py`: reproduce las muestras de red que recibió el invitado (captura) con la lógica de `vfl::Track` para
  varios retrasos y las compara con la pose real del anfitrión (su captura) unos ms antes.
- `vehcands.py pid`: los gimmicks de vehículo con padre (vtable, controlador, hijos). Así se vio (13-nov, noche) que
  los caballos de los establos llevan 8–12 cada uno y que la regla de v20 no era solo del barco.
- `vfmon5.py host guest segundos [periodo]`: el vehículo `0xB010000A` en las dos copias (posición, yaw, velocidad) y la
  distancia; la medida de la navegación de las 18:27.
- `shipparts.py pid`: el árbol de piezas que cuelgan de `0xB010000A`. `shiplocal.py pid lx lz ...`: un punto en el
  sistema del barco → mundo (para ir al timón o a la escalera con el barco en otra pose).

## 14-nov-2026 (build 1.0.0.3663): el timón, los pasajeros

Ver `docs/research/port-3663.md`, «Barco en 3663». Usan `../livetest/cdbuild.py` (slots de la build instalada).

- `tsprobe.py pid clave [s]`: el TransformSync de un actor: pose local, marco del padre, pose de mundo, y los tipos de
  petición pendientes vistos en `TS+0x5BC` al muestrear (así salieron el 7 de cerca, el 12 de lejos y el 12 de los
  personajes).
- `partlocal.py host guest`: la pose local de cada pieza del barco en las dos copias, lado a lado (el timón: 0,81 /
  0,29 en el anfitrión, 3,76 / 2,14 en el invitado). Cambia el rango de claves de la sesión dentro del script.
- `tsflags.py`, `hdiff.py host guest clave comp tam`: banderas del TS y volcado de un componente en las dos copias
  con las filas que difieren.
- `ptrpath.py pid desde hasta [prof]`: caminos de punteros de un objeto a otro (el objeto de escena de un actor:
  `[[comps+0x30]+0x18]-0x28`). `scenechain.py pid objeto`: la cadena de enganche de un objeto de escena y su
  transformación local (`+0x1D0`). `scenelocal.py host guest clave_min clave_max`: la local del objeto de escena de
  cada pieza junto a la del TS, en las dos copias.
- `keyscan.py captura.bin clave [t0 t1]`: todos los mensajes de una captura que nombran una clave, por dirección y
  opcode (nombres del `.log` hermano; vale para cualquier build).
- `ridemon.py host guest s [periodo]`: el vehículo en las dos copias y el personaje del invitado (padre y local).
  `passmon.py host guest clave_anfitrión clave_invitado s`: a cuánto de su sitio en el barco dibuja cada cliente al
  personaje del otro.
- `poke3f.py pid dirección x y z`: ESCRITURA de prueba de tres floats (con ella se vio que corregir la local del objeto
  de escena del timón se queda). Solo en la copia del invitado y con copia de las partidas.
- `hw_watch.cpp`: ahora guarda los registros en cada sitio y, en modo `sites`, el qword de `[rbp + x]` (cuarto número
  del `.cfg`): con `696` (0x2B8) da el objeto de escena dentro del tick del TransformSync `0xB3A3B0`.
- `parents.py captura.bin clave`: qué padres nombran los `MoveActorReq` / `MoveActorAck` de un actor, por dirección (así
  se vio que el servidor solo conserva el padre «soporte del timón» y manda en blanco los de cubierta). `ackdump.py`:
  un `MoveActorAck` con padre y otro sin él, campo a campo. `../livetest/shipinv.py pid [x z]`: pose del barco y un
  punto del mundo en su sistema.

## 14-nov (mañana): capturas `.bin`, piezas del barco, velas

Las capturas `.bin` de `bin64` guardan los paquetes ENTEROS con los códigos NATIVOS de la build (3663: tabla
`kEntries3663` de `src/common/native_opcode_table.inc`); el `.log` solo trae el principio de cada paquete.

- `keyhist.py captura.bin clave_lo clave_hi [t0 t1] [x]`: mensajes por clave (los que empiezan por esa clave), por
  dirección, con primera y última hora; con el sexto argumento, sin los `Move`.
- `pktdump.py captura.bin Nombre clave [t0 t1] [max] [uniq]`: los paquetes de un mensaje con sus bytes.
- `gimevents.py captura.bin clave_lo clave_hi`: por gimmick, cada par (evento, parámetro) de sus `UpdateGimmickReq`
  con cuántos y cuándo, y cuántos 2609 / 3082 / 2779 mandó. Así se ve que la vela y los cañones comparten eventos y
  difieren en el estado (el parámetro).
- `recnames.py captura.bin clave_lo clave_hi [t0 t1]`: de cada registro 2005 de esas claves, su tipo, controlador,
  nombre (longitud y texto) y las dos copias del padre que van DESPUÉS del nombre (`+0xB9` / `+0xD7`), junto a lo
  que hay en `+0xF5` / `+0x113` fijos (lo mismo solo si el nombre tiene 13 caracteres).
- `npcmon.py anfitrión invitado segundos [periodo] [raíz]`: los actores que van en el barco del anfitrión sin ser
  piezas suyas (la tripulación: tipo 4, clave 0xB, colgados de una pieza del casco), tal como los tiene cada copia: en
  el anfitrión su padre y su posición local; en el invitado si está, de qué cuelga allí y a cuántos metros lo dibuja
  de su sitio (la copia del invitado del padre del anfitrión, compuesta con la local del anfitrión). Así se midió que
  con el barco a 2 m/s el invitado dejaba a casi toda la tripulación sin padre, a 12–31 m de su sitio.
- `vehcmp.py anfitrión invitado [min_piezas]` y `vehwatch.py anfitrión invitado segundos [periodo]`: cada barco del
  mundo del anfitrión (vehículo de tipo 5 con 6 o más gimmicks colgando) tal como lo tiene cada copia: si es de un
  jugador o de la IA, a qué velocidad va, a cuántos metros está la copia del invitado y a cuántos su última pose de
  red. `vehwatch` lo apunta cada pocos segundos (sirve mientras alguien juega: solo lee memoria).
- `movedump.py captura.bin clave[-clave] [t0 t1] [max]`: los `MoveActorAck` de un actor, descifrados (padre, posición
  local, posición del mundo). Los de la tripulación llegan al invitado EN BLANCO (identidad, sin padre), uno por
  segundo.
- `reqsample.py pid segundos [clave]`: muestrea a toda velocidad la petición de posición pendiente del
  `TransformSync` del personaje propio (o de un actor) y la imprime junto a su padre y su posición local. Con el
  invitado de pie en la cubierta (padre = el casco) se ve que sus peticiones de tipo 12 llevan la posición del MUNDO:
  una petición sirve igual para un actor con padre que sin él.
- `cr_extract.py captura.bin salida.stream` y `cr_replay.cpp` (`cr_replay salida.stream delay_ms [on] [fail=K,..]
  [lost=K,..] [budget=N]`; cómo compilarlo, en su cabecera): los 2005 / 2516 / 2174 reales de una captura del
  invitado por la lógica de `create_retry` (`src/mod/session2/create_retry_logic.h`, tal cual), contra un modelo del
  cliente en el que todo actor anunciado existe `delay` ms después salvo los de `fail=` (fallan, con un fallo sin
  clave) y `lost=` (no aparecen, sin error). Dice cuánto guarda, cuánto tarda y qué volvería a entregar. Sin juego.
- `hashrefs.py [hex8...]`: dónde aparece en el `.exe` en disco (3663) un valor de 4 bytes tal como va en el paquete,
  con la instrucción que lo lleva. Los hashes de estado de la vela no aparecen: salen de los datos del juego.
- `loadrecs.py captura.bin clave_lo clave_hi [t0 t1]`: los registros del `ActorLoadDataAck` de esas claves, por lote,
  con la familia de su cola. `recdiff.py captura.bin claveA claveB [t0 t1] [dump]`: dos registros byte a byte.
- `shiptree.py pid_anfitrión pid_invitado [raíz]`: todo lo que cuelga del vehículo en las dos copias, lado a lado
  (tipo, padre, enganche, posición local, y en cuál falta). `actorinfo.py pid clave...`: un actor por su clave.
- `branchfind.py pid_anfitrión pid_invitado clave [hash...]`: componentes del actor con su clase y dónde aparece cada
  hash de estado de rama (por defecto, los de la vela).
- `setctl.py pid clave [own | controlador] [--write]`: el controlador de un actor en una copia; con `--write`, lo
  cambia (EXPERIMENTO: escribe en la memoria del juego).
- `weatherdump.py pid [pid2]`: los bloques del clima en crudo, o lo que difiere entre dos copias.
