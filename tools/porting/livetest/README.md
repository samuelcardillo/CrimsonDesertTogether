# Prueba en vivo con dos copias, automatizada (14-nov-2026, build 1.0.0.3663)

Lo que se usó la noche del 14-nov para probar el barco sin nadie al teclado. Todo es solo lectura de memoria más
entrada de teclado y ratón a la ventana del juego. `python -I`, Git Bash y `pwsh`. Antes: Steam abierto, las dos copias
cerradas (el lanzador hace copia de las partidas en `backups/saves-2inst-*`).

- `ui.ps1 -GamePid <pid> -Do shot|key|hold|look|click|lmb|mmb|wheel|drag ...`: entrada solo a la ventana de ese proceso
  (comprueba que está delante antes de mandar nada) y capturas (`-Out`; `UI_CROP=x,y,w,h` recorta a resolución real).
  **No manda ALT si la ventana ya está delante:** hasta el 14-nov mandaba un toque de ALT en cada acción, y el
  personaje seguía corriendo tras un W corto.
- `cdbuild.py`: el slot del mundo y el de la cámara de la build instalada (3576 o 3663).
- `posnow.py pid`: posición del personaje propio ahora mismo (local a su padre si lo tiene, y la clave del padre).
- `face.py pid x z`: gira la cámara hacia un punto del mundo. `walk.py pid [-s parada] [-n pasos] x z [x z ...]`: anda
  a puntos del mundo con W cortos, leyendo la posición en vivo (vale nadando). `goto3576.py`: lo mismo, más rápido en
  tramos largos (corre), menos fiable cerca de obstáculos.
- `shiplocal.py pid lx lz ...`: puntos del sistema del barco (`0xB010000A`) → mundo. `shipwalk.py pid lx lz ...`: anda
  por la cubierta a puntos del sistema del barco (el barco amarrado gira: 36°, 47°, 66° en tres sesiones).
  `shipparts.py pid`: el árbol de piezas.
- `map_tp.py pid host|guest x z`: viaje rápido al nexo más cercano a un punto, por el mapa (usa `tp.sh` y `nexus.py`:
  doble clic en el icono del nexo y Espacio; `nexus.py` elige la mancha azul más cercana al punto, porque el punto
  azul del otro jugador en el mapa desviaba el clic). `mapcal.py pid host|guest`: con el mapa abierto, el punto del
  mundo del centro de la pantalla y la escala (dos pines). Al máximo de ampliación (0,04 m por píxel) el mapa enseña
  calles y plantas de edificios: sirve para sacar una ruta a pie.
- `launch_house.ps1` (cierra, despliega, lanza las dos copias y mete al anfitrión en el mundo) y `run_join.ps1 -Attach
  -AttachHost <h> -AttachGuest <g>` (une al invitado).
- `shipsession.sh [etapa] [H G]`: de cero a «anfitrión al timón, invitado en la cubierta de popa» (~25 min): lanzar,
  Vellua, Hopkins, sacar el barco (2600, no queda guardado), unir al invitado, subir los dos por la escala de babor.
  Etapas: `launch` (todo), `hopkins`, `join`, `guestboard`, `hostboard`. `hop.sh H`: solo apuntar a Hopkins y R.
- `sailtest.sh H G` (tras `SAIL_EXP=1 bash shipsession.sh`, que lanza con `profiles/host-sail-exp.ini` y
  `profiles/guest-sail-exp.ini`: los perfiles del kit más los experimentos): los experimentos de la vela y los
  faroles del 14-nov. Perfiles: el del anfitrión con `gimmick_relay=1` (y `send_trace=2609,2758`), el del invitado
  con `gimmick_take=1`, `create_retry=1` y `create_retry_lost=8`. Paso 0: piezas del barco en las dos copias (`boat/shiptree.py`) y lo que apuntó `create_retry`;
  pasos 1 a 3: primera marcha (LSHIFT 1,5 s), segunda, y recoger (ALT 2,5 s), cada uno con lo que reenvió el
  anfitrión, lo que hizo el invitado (tomar el control, peticiones retenidas, errores del juego) y una captura de
  cada copia mirando a la arboladura; la clave de la vela sale de la captura del anfitrión y `boat/branchfind.py`
  dice dónde vive el estado en las dos copias. Paso 4, solo si el invitado no tomó la vela por sí mismo: se le pone
  de controlador a mano (`boat/setctl.py --write`) y se repite la primera marcha. Capturas `shots/sail_*.png`.
  Ejecutado el 14-nov por la tarde: los pasos sirven, pero LSHIFT no siempre engrana a la primera (ver abajo) y las
  capturas «mirando arriba» no llegan a enseñar las velas (la cámara del timonel apenas sube).
- `statdump.py pid`: las entradas de estadísticas del personaje propio tal como las recorre `PlayerReader` (id,
  actual, máximo, regeneración). `statwatch.py pid segundos`: cuáles CAMBIAN en ese rato y entre qué valores.
  `statseries.py pid segundos id [id ...]`: el valor de esas entradas cuatro veces por segundo. Con ellas se vio el
  15-nov que el aguante en 3663 es el id 25 (el 22 de las builds anteriores no existe): con W y SHIFT seis segundos
  baja 440 → 410,7 → 383,5 y vuelve a 440 dos segundos y medio después de soltar; 26 (170) y 27 (120) no se mueven.
- `nearactors.py pid [radio] [tipos]`: los actores cercanos al personaje propio (por defecto los de tipo 3), con su
  distancia, posición y vida si tienen componente de estado: para buscar un enemigo y mirar su vida desde las dos
  copias. `actorhp.py pid clave [clave ...]`: la vida de esos actores en una copia, y la del personaje propio.
  `caminfo.py pid`: a qué distancia y altura está la cámara del personaje, con su inclinación y rumbo; `pitch.py pid
  grados` pone la inclinación.
- `fightcheck.sh etapa H G` (con los dos en el mundo; 15-nov, la primera pelea vista en 3663): `travel` lleva a los
  dos por viaje rápido al nexo junto a Valvegard Garrison (-4710, -5348), en medio de los mechas; `list` da los
  enemigos cercanos de cada copia; `hit CLAVE [n]` hace que el invitado se gire hacia ese enemigo, se acerque y dé n
  golpes con el botón izquierdo, y cada cuatro dice la vida del enemigo en las dos copias y la del invitado;
  `kill CLAVE [n]`, lo mismo sin pausas hasta que el enemigo muere o se acaban los golpes (sin andar si ya está a
  mano); `pick`, la clave del enemigo más cercano al invitado con 100–200 de vida y a su altura; `report`, lo que dice el
  log del anfitrión (`User2Funnels rank` / `dead`), errores y volcados. Los dos mueren enseguida ahí: revivir en el
  punto de control es MANTENER U (`ui.ps1 -Do hold -Keys U -HoldMs 2000`). Ojo: el nexo está en lo alto de un
  peñasco y el guion anda y apunta en planta; en cuanto el invitado cae a la repisa de abajo pega a la roca (así
  acabó la etapa `kill` la única vez que se usó).
- `sailcrew.sh H G` (anfitrión al timón, invitado en cubierta): lo que se pidió al principio del trabajo del barco,
  sobre el `cdmp.asi` que haya instalado: el estado de la vela en las dos copias tras cada marcha
  (`sailstate.py pid key` da la clave de la vela, `sailstate.py pid CLAVE` su estado) con lo que apuntó cada mod, y a
  cuánto de su puesto dibuja el invitado a cada tripulante con el barco en marcha (`boat/npcmon.py`, resumido).
- `slidetest.sh H G [s]`: cuánto resbala un pasajero por la cubierta en la copia de detrás a toda vela
  (`boat/deckdrift.py` y, fotograma a fotograma, `boat/slidetrace.py`). `ASI=<un cdmp.asi> bash shipsession.sh`
  arranca la sesión con ese `cdmp.asi` (el de un kit publicado: `dist/…/cdmp.asi`) para comparar builds en la misma
  prueba; el arranque siguiente sin `ASI` vuelve a poner el compilado.
- `shiphead.py pid`: el rumbo de la proa del barco (su eje -z; 90 = +x, el este) y su posición. Para gobernar una
  prueba de velocidad: el barco encalla enseguida al sur y al suroeste del amarre (bajíos en (-10410, -6373) y
  (-10075, -6913), 15-nov); una y sobre el agua pasa de 350 es que está encallado.
- `nearmem.py pid`: las regiones ejecutables justo debajo de la imagen del juego (donde van los trampolines de
  los ganchos): los tres bloques de 64 KB que el mod coge al arrancar y cualquier otro. Si el log del anfitrión
  trae líneas «no trampoline memory» o `REFUSED`, esa sesión no vale: el mod va a medias (15-nov, 04:31).
- `tagcheck.sh H G [aboard]` (anfitrión al timón, invitado en cubierta): el cartel del otro jugador navegando
  (`boat/tagmon.py`, la línea `Overlay diag` de cada copia y una captura desde cada lado) y, con `aboard`, si el
  invitado sigue a bordo a toda vela con la copia del anfitrión delante.
- `smoothtest.sh H G` y `smooth2.sh H G [clave_IA ...]` (con el anfitrión al timón y el invitado en cubierta): zarpar
  y medir en el invitado cómo se mueve su copia del barco del anfitrión (`boat/smoothmon.py`), los carteles
  (`boat/tagmon.py`), los demás barcos en las dos copias (`boat/vehwatch.py`) y lo que dice el seguidor del invitado.
  `smooth2.sh` mete la segunda marcha con el anfitrión delante y mide con el invitado delante (ver «la copia de
  detrás va lenta», abajo). `reboard.sh pid`: sube a un personaje que se ha quedado en el agua o en el muelle.
- `shopsession.sh`: de cero a «el invitado compra una Water a Delkin y un saco en el puesto de al lado» (Hernand,
  ~25 min): lanzar, anfitrión al nexo de Hernand y a la calle de las tiendas, unir al invitado, llevarlo al
  mostrador, R, comprar, salir, y E mantenida en el puesto. No vende nada (vender exige mirar antes la pantalla).

## Lo que hay que saber

- **Hopkins (gestor de barcos de Vellua):** el aviso «R Manage Ship» sale en blanco solo desde
  (-10471,9, 352,5, -6138,1…-6138,3), con la cámara hacia (-10468, -6145) y `look -Dx 10 -Dy 20`; en gris o sobre el
  saco de trigo, R abre el mapa. No depende de la hora. R mantenida 2 s; Espacio, Espacio, Esc.
- **Barco amarrado:** origen ~(-10372, 350, -6207); escala de babor en local (-4,37, -0,74, -4,21): nadar a local
  (-9, -7,7), luego (-5,3, -4,3), mirar a (-3, -4,21) y W 3,5 s + 3 s. Cubierta: (-2,9, -1,2) → escalera (-2,85, 0,6)
  → cubierta de popa (-2,78, 2,27); timón en (0, 1,85): desde ahí, un paso hacia el timón y E. Al timón: LSHIFT 1,5 s
  = una marcha (3,4 m/s; dos, hasta 13 m/s), A/D giran, ALT frena. El barco encalla con facilidad al sur de Vellua.
- **Las marchas no siempre entran:** el 14-nov a las 14:55, recién tomado el timón, LSHIFT 1,5 s y 2,5 s movieron
  el barco (2 m/s) sin que la vela cambiara de estado; tras soltar el timón (R) y volver a tomarlo (E), LSHIFT 1,5 s
  dio la primera marcha (3,5 m/s y los estados de la vela). Si a los 10 s no hay cambio de rama de la vela, R, E y
  otra vez. La vela es el hijo directo del vehículo con local (0, 0, 0): `0xB010089A` por la mañana, `0xB01013CA`
  por la tarde (casco + 0xA). Sus estados cambian también solos (viento, velocidad), sin tocar nada.
- **Un carro del mercado de Vellua** aparca algunos días en (-10501,8, -6145,6), justo en la recta del nexo al muelle:
  el personaje corriendo se sube a su caja y no sale (W no hace nada; solo otro viaje rápido lo saca).
  `shipsession.sh` pasa ahora por el norte (`past_wagon`).
- **Hernand:** nexo en (-10626,4, -3813,3). A Delkin (mostrador (-10655,4, -3877,2), mirar a (-10650,0, -3881,5))
  se va por el arco: (-10637,0, -3823,5) → (-10641,5, -3827,0) → (-10645,6, -3831,6) → (-10650,1, -3835,2) →
  (-10655,6, -3840,2) → (-10661,7, -3845,7) → (-10663,2, -3856,9) → (-10662,5, -3869,3). En línea recta desde el
  nexo se acaba encima del muro de un huerto. Puestos: junto a Delkin desde (-10653,9, -3876,4) mirando a
  (-10648, -3873) («Harvest Sack»), y el del almacén general desde (-10645,5, -3862,4) («Rosemary»). Delkin
  atiende también por la tarde (18:43 de juego). `run_join.ps1` puede decir «guest idle: False» con el invitado ya
  dentro: la prueba es `posnow.py`. El invitado aparece en su último nexo, no junto al anfitrión.
- La copia que no está delante va a ~7 fotogramas/s: las medidas de «un fotograma de retraso» en esa copia salen
  veinte veces peores que con ella delante. **Y su mundo va lento:** el 15-nov el barco en primera hacía 3,3 m/s con
  el anfitrión delante y ~1 m/s en cuanto se ponía delante el invitado (`boat/smoothmon.py` en el anfitrión: 97
  fotogramas en 20 s). Lo que se mida en el invitado con el barco «en marcha» hay que medirlo sabiendo eso
  (`smooth2.sh`: segunda marcha y `smoothmon.py … 0.5`).
- **La escala del barco (15-nov):** con W mantenida 3,5 s + 3 s + 0,9 s, como hasta entonces, el primer tramo ya
  llegó a la cubierta y los siguientes cruzaron la cubierta y bajaron por otra escala: el invitado acabó en el agua y
  el anfitrión colgado de la escala, y las etapas decían «on deck» y «helm» (el barco no se movió en toda la prueba
  siguiente). `shipsession.sh` sube ahora en tramos de 1,2 s mirando después de cada uno dónde está el personaje
  (`where`: `deck`, `ladder`, `wheel` u `off`, por su padre y su posición local), da un paso hacia dentro al llegar
  arriba (justo en lo alto, un W corto vuelve a agarrar la escala) y cada etapa falla si no acaba donde dice. El
  timón se toma con E desde dos metros («Ship's Wheel / Operate»), también con el otro jugador en medio.
- **Hopkins, otra vez la postura:** el 15-nov a las 02:04, desde (-10471,93, -6138,02) el aviso no salió y R abrió
  el mapa; una hora antes había salido desde (-10471,81, -6138,06), y salió desde (-10471,85, -6138,49) tras un paso
  corto con W hacia él. La etapa exige ahora z al sur de -6138,15. **No basta:** a las 03:55, desde
  (-10471,91, -6138,47) y desde (-10471,57, -6138,75), el foco se quedó en un saco de trigo («Wheat / Carry») con
  todas las punterías probadas (`look` 10/20 y 30/50, cámara a -7° con `pitch.py`, rumbo 121° a 151°, también en
  primera persona, donde el foco fue a un reloj de arena de su mesa); R abre entonces el mapa, y tras
  Espacio-Espacio-Esc el mapa sigue abierto (otro Esc lo cierra). Tres sesiones de cinco lo consiguieron a la
  primera con la misma etapa. Sin regla todavía: lo que se sabe es que el aviso «Hopkins … / CTRL» de abajo a la
  derecha sale siempre y no es el de la interacción, que CTRL pasa el foco de un saco a otro, y que la rueda del
  ratón acerca y aleja la cámara (20 pasos la dejan en primera persona; `caminfo.py` dice a qué distancia está).
- Medir: `tools/porting/boat/ridemon.py` (barco en las dos copias y el invitado en cubierta) y `passmon.py` (a qué
  distancia de su sitio dibuja cada cliente al otro jugador).
