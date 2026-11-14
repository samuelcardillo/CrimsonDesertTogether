# Launcher de jugadores (`cdmp_launcher.exe`)

Hecho el 2026-11-14; el mismo día se le añadieron la actualización desde GitHub y las partidas (copia de seguridad y
progreso de la sesión, [más abajo](#las-partidas-copia-de-seguridad-y-progreso-de-la-sesión)). Instala o actualiza
el mod en la carpeta del juego, arranca el juego con el mod o sin él, escribe con quién se juega y, con el juego
cerrado, pasa a la partida real lo que el jugador avanzó en una sesión. No tiene ganchos ni toca el juego: descarga
y copia archivos, escribe `cdmp.ini` y lanza `steam://rungameid/3321460`. Lo que ve el jugador está en inglés.

## Dónde está

| Qué | Archivo |
|---|---|
| Reglas, sin Windows (se prueban solas) | `tools/launcher/launcher_logic.h` |
| Archivos, registro, descarga | `tools/launcher/launcher_core.cpp` |
| Reglas de las partidas (qué progreso se pasa y cuál no) | `tools/launcher/save_logic.h` |
| Partidas: copia de seguridad, búsqueda y cambio de ranura | `tools/launcher/save_core.cpp` |
| Ventana (Dear ImGui sobre Direct3D 11) | `tools/launcher/main.cpp` |
| Pruebas | `tests/launcher_tests.cpp` (ctest `launcher`) |
| En el kit | `scripts/package_coop.ps1` mete el exe, `package.txt` y `kit/coop/cdmp_launcher.ini` |
| Publicación de una release (desde un PC) | `scripts/publish_release.ps1`, `scripts/fetch_loader.ps1` |
| Para qué build del juego es el kit | `kit/coop/game-builds.txt` |

Para probarlo sin tocar el juego real:
`cdmp_launcher.exe --game-dir <bin64 falso> --data-dir <carpeta> --bundle <carpeta del kit> --no-launch`.
Un `bin64` falso sirve con los primeros 4 KB del `CrimsonDesert.exe` real (de ahí lee la build). `--offline` no
pregunta a la dirección de actualizaciones (para probar un kit local más nuevo que la última release) y
`--press update|play|keep` pulsa ese botón en cuanto termina de mirar el PC, para pruebas sin ratón
(`--press settings` abre la última sección, que una prueba no puede pulsar).

**En una prueba pon siempre `--local-dir <carpeta>`**: hace de `%LOCALAPPDATA%`. Sin él el launcher mira las
partidas reales del PC (`%LOCALAPPDATA%\Pearl Abyss\CD\save`) y, si hay progreso de una sesión esperando, lo pasa a
ellas sin preguntar (es lo que hace para un jugador).

## De dónde sale el mod: se publica desde el PC de desarrollo, el launcher descarga

Los jugadores no tienen compilador (las VS Build Tools son varios GB), así que el mod no se compila en su PC. Se
compila en el PC de desarrollo y se publica como release del repositorio en GitHub; el launcher baja esa release.
En GitHub no se compila nada.

1. Con el commit ya subido: `.\scripts\publish_release.ps1 -Publish`. Se niega si hay archivos con cambios o si el
   commit no está en el remoto. Después ejecuta `scripts/build.ps1 -Clean` (compila TODO de nuevo, pone en el mod el
   id de ese commit y pasa los tests: si falla uno, no se publica; la compilación limpia es porque `b1` salió con un
   objeto obsoleto y se cierra al arrancar de anfitrión, `docs/research/objetos-obsoletos-msbuild.md`), baja el cargador ASI si falta (`fetch_loader.ps1`, comprobando su
   SHA-256), hace el kit (`package_coop.ps1`) y crea la release con `gh release create`, con el `gh` que tenga la
   sesión iniciada en el PC.
2. La release se llama como el kit, `cdmp-coop-<fecha>-<build del juego>-b<n>` (`n` es uno más que el número más
   alto entre las releases; `-Number` lo fija), y lleva sueltos `package.txt`, los cuatro archivos que instala el
   launcher, `cdmp_launcher.exe`, el zip del kit y los símbolos del mod (`...-symbols.zip`, para leer volcados).
3. El launcher lleva compilada la dirección
   `https://github.com/<repo>/releases/latest/download/package.txt` (`CDMP_UPDATE_ADDRESS` en `CMakeLists.txt`).
   Al arrancar la lee; los archivos que nombra los baja de al lado, comprobando el SHA-256 de cada uno.

Sin `-Publish` el script solo hace el kit en `dist\` con la build que haya en `build\`: nada sale del PC.

**Publicar una release es lo que entrega código a los jugadores:** su siguiente Update la instala. Un push a `main`
ya no publica nada. Publica solo lo que deban ejecutar los dos.

Se compila con VS 2022 (MSVC 14.4x), el compilador con el que se prueba el mod. El de VS 2026 (MSVC 14.51) genera
código incorrecto para este proyecto (`docs/research/msvc-14.51-miscompila.md`): `build.ps1` elige VS 2022 aunque
esté instalado el otro y se niega a compilar con él, así que una release no puede salir con ese compilador.

**Historia (2026-11-14).** El repositorio es público desde ese día (con él privado GitHub contesta 404 a quien no ha
iniciado sesión y el launcher dice "Could not check for updates (the server answered 404)"). Hubo un workflow de
GitHub Actions (`.github/workflows/release.yml`) que compilaba y publicaba en cada push a `main`. No llegó a
ejecutar un solo paso ("The job was not started because your account is locked due to a billing issue") y se quitó
ese mismo día por decisión del dueño: las releases se hacen siempre desde este PC. La primera,
`cdmp-coop-20261114-3663-b1` (commit `75bc963`), ya se publicó así; se cerraba al arrancar de anfitrión por un
objeto obsoleto de `build\` (`docs/research/objetos-obsoletos-msbuild.md`) y la sustituyó esa tarde
`cdmp-coop-20261114-3663-b2` (commit `a4868ef`), la primera hecha con `publish_release.ps1 -Publish` de principio a
fin (compilación limpia, 105 pruebas, kit, `gh release create`: 7 min).

La build del juego para la que es el kit (`game=` en `package.txt`) sale de `kit/coop/game-builds.txt`, igual en
cualquier PC. `package_coop.ps1` avisa si el juego instalado en el PC no está en esa lista.

## Qué hace

1. **Encuentra el juego**: `HKCU\Software\Valve\Steam\SteamPath` → `steamapps\libraryfolders.vdf` →
   `appmanifest_3321460.acf` → `<biblioteca>\steamapps\common\<installdir>\bin64`. Se puede elegir la carpeta a mano.
2. **Lee la build del juego**: el timestamp del PE (los primeros 4 KB del exe) y la versión del recurso de versión.
3. **Busca paquetes del mod**, en este orden: lo que lista la dirección de actualizaciones (el más nuevo primero) y
   después lo que hay en el PC, el más nuevo primero según la fecha de su `package.txt`: la carpeta del kit (donde
   está el exe) y las copias que guardó antes en `%LOCALAPPDATA%\cdmp_launcher\packages\<id>\`. Usa el primero que
   nombre la build instalada; si ninguno la nombra, un kit sin `package.txt` (los anteriores al launcher) se usa
   avisando de que no dice para qué build es. Si solo hay paquetes de otra build, no deja jugar con el mod y lo dice.
   El launcher funciona solo, sin carpeta de kit: le basta la dirección de actualizaciones.
4. **Botón Update** (en la línea "Mod"): baja el paquete elegido a la carpeta del launcher y, si el modo elegido es
   Join u Host, lo instala y escribe `cdmp.ini`. No arranca el juego. Con el juego abierto solo descarga. Si no hay
   nada que actualizar, el botón es "Check for updates" (vuelve a preguntar). Si la dirección no contestó, el botón
   dice "Install", porque no se sabe si lo que hay en el PC es lo más nuevo.
5. **Botón de jugar**:
   - *Join a friend* / *Host a game*: lo mismo que Update (descarga si hace falta, comprobando el SHA-256 de cada
     archivo), instala `winmm.dll` y `cdmp.asi` en `bin64`, escribe `cdmp.ini` desde la plantilla del papel
     (`cdmp.ini.guest` / `cdmp.ini.host`) con `player_name` y `peer_steamid`, y lanza el juego por Steam. Si eso
     cambia la versión que había en la carpeta del juego, el botón lo dice ("Update and join ...").
   - *Single-player*: quita `cdmp.asi` y el cargador de `bin64` y lanza el juego. `cdmp.ini` se queda.
   - Con el juego abierto no escribe nada.
6. **Con quién**: el invitado elige al anfitrión (`peer_steamid`; vacío = el único amigo jugando). El anfitrión
   puede limitar quién entra (su `peer_steamid`; vacío = cualquier amigo jugando). La lista de nombres sale de la
   caché local de Steam (`userdata\<cuenta>\config\localconfig.vdf`, bloque `friends`): no dice quién está jugando
   y puede traer gente que no es amiga. También se puede pegar un SteamID64 o un enlace `/profiles/<id>`.

## Las partidas: copia de seguridad y progreso de la sesión

**El problema.** Con el mod puesto (`save_guard=redirect`) el juego guarda en una caja y la carpeta real
(`%LOCALAPPDATA%\Pearl Abyss\CD\save`) no se escribe nunca. Lo que un jugador avanza en una sesión queda fuera:

- **Anfitrión**: sus guardados están en la caja, `...\CD\cdmp_save_sandbox`. El siguiente arranque con el mod la
  rehace desde la carpeta real (la anterior va a `bin64\cdmp_save_exports`, `docs/research/s25/s35-fase-e-guardado.md`).
- **Invitado**: su personaje lo guarda el juego del anfitrión y vuelve por el túnel como generaciones en
  `%LOCALAPPDATA%\cdmp\guest_progress\<SteamID64 del anfitrión>\incoming\gen<n>\`
  (`docs/research/noche/progreso-invitado.md`).

Hasta el 14-11 pasarlo a la partida real era cosa del jugador, con dos scripts de PowerShell del kit
(`apply-guest-progress.ps1`, `host_save_restore.ps1`) y una copia de seguridad a mano. En la práctica nadie lo
hacía: en este PC la carpeta real seguía con fecha del 2 al 4 de noviembre tras dos semanas de sesiones.

**Qué hace el launcher** (siempre con el juego cerrado; `save_core.cpp`):

1. **Copia de seguridad**: la carpeta `save` entera a `%LOCALAPPDATA%\cdmp_launcher\save_backups\<fecha-hora>\save`,
   con `backup.txt` (tamaño y SHA-256 de cada archivo), comprobando cada copia. No repite una copia si la carpeta es
   igual que la última; quedan las 10 más nuevas. La hace antes de arrancar el juego con el mod (Join / Host) y antes
   de escribir nada en la carpeta real; sin copia comprobada no escribe.
2. **Busca el progreso que espera**: al abrirse, 10 s después de que el juego se cierre (deja terminar a Steam) y
   antes de arrancar el juego.
   - *Del anfitrión*: cada ranura 0-2 / 100-108 de la caja que no sea igual que la real. Solo si la caja la hizo un
     juego anfitrión: tiene que existir el diario de esa sesión (`cdmp_save_exports\runs\`, el que escribe
     `host_save=probe`) con el id de carpeta de la caja. La ranura se pasa si **todos** los guardados que fueron a
     ella son del anfitrión con identidad positiva y resultado 0, los dos archivos tienen cabecera válida y se
     escribieron juntos (10 s), no hay restos `slotN_new` / `slotN_backup` y la ranura real no se ha escrito después
     de empezar esa sesión (si el jugador siguió sin el mod, su partida nueva se queda). Si no, se deja y la línea
     "Saves" dice por qué.
   - *Del invitado*: de cada anfitrión, la generación más nueva que encaja en su ranura, con la regla de línea de
     `apply-guest-progress.ps1` (solo sobre la partida de la que viene, una generación suya o una ranura vacía, y
     nunca hacia atrás) y con sus dos archivos comprobados contra el manifiesto. Si la ranura tiene ya otra partida
     no se dice nada: el jugador siguió por su cuenta. Las carpetas que no son un SteamID64 (`unknown-host`, de las
     pruebas sin Steam de este PC) no se miran.
   - Si esperan las dos cosas para la misma ranura, vale la más reciente.
3. **Lo pasa**: monta la ranura nueva en `...\CD\cdmp_launcher_staging\slotN` (al lado de `save`, mismo volumen),
   la comprueba, saca la vieja y mete la nueva con dos renombrados y relee lo que quedó; si algo falla la ranura
   vuelve a estar como estaba. Nunca crea nombres `slotN_new` ni `slotN_backup` dentro de `save` (el juego los toma
   por un guardado suyo a medias). Para el invitado escribe `applied.txt` como el script (el mod se lo dice al
   anfitrión). Cada cosa hecha queda en `%LOCALAPPDATA%\cdmp_launcher\saves.log`.
4. **Sin preguntar**, por defecto (`keep_progress=1` en sus ajustes; casilla "Put my co-op progress into my save
   after each session"). Con la casilla quitada la línea "Saves" lo dice y ofrece el botón "Keep progress".
5. **Al pulsar Play**: primero pasa lo que espere. Si no puede (un archivo en uso, sin sitio para la copia), no
   arranca el juego y lo dice; al pulsar otra vez arranca sin pasarlo.

**En qué se aparta de los scripts.**

- El del anfitrión exigía Steam cerrado; el launcher no (arranca el juego por Steam). **[NV]** qué hace Steam Cloud
  con una ranura cambiada con Steam abierto y el juego cerrado.
- El del anfitrión partía de una generación de `cdmp_save_exports` y de su veredicto `probe`. El launcher lee la
  caja directamente (tiene que actuar antes del arranque que la exporta) y no usa `candidate`: ese veredicto pide
  que no haya ningún Flush diferido, y **así se escribe todo autoguardado** (el "flush timer" 5 s después). En las
  33 generaciones de este PC, las 47 ranuras cambiadas salieron `unknown` por ese motivo y ninguna `candidate`. La
  regla del launcher (`judge_host_slot`) acepta el Flush diferido si es del anfitrión con identidad positiva.
- El script `host_save_restore.ps1` solo conocía las builds 2976, 3527 y 3549 y rechazaba las generaciones de 3663:
  corregido el 14-11 (3576 y 3663 añadidas).

**Lo que no está demostrado** (por eso hay copia de seguridad antes de cada cambio, y la casilla):

- No se ha ejecutado contra la carpeta real ni en una sesión jugada: solo los tests, con carpetas falsas.
- Nadie ha cargado todavía en el juego una partida de anfitrión guardada durante una sesión con invitado. La
  auditoría S34 §5 deja abierto que el mundo guardado sea coherente (M2); el diario prueba quién guardó, no eso.
- Si el anfitrión arranca el juego desde Steam sin pasar por el launcher, la caja se rehace y el progreso de la
  sesión anterior se queda fuera (en `cdmp_save_exports`, para el script).
- En el mundo del anfitrión el invitado ve los puntos Abyss Nexus del anfitrión: no se sabe si acaban en su partida.

## Reglas que conviene conocer

- **`winmm.dll` ajeno no se toca.** Es "nuestro" si su SHA-256 es el del paquete o el que el launcher instaló antes
  (`installed_loader_sha256` en sus ajustes). Cualquier otro se deja, también al quitar el mod. Si hay otro `.asi`
  en `bin64`, `scripts\` o `plugins\`, nuestro cargador se queda al pasar a un jugador.
- **`cdmp.ini`**: el launcher marca su primera línea (`; cdmp_launcher role=… template=<sha256>`). Mismo papel y
  misma plantilla: solo cambia `player_name` y `peer_steamid`, lo que el jugador haya tocado se conserva. Otro papel
  u otra plantilla (kit nuevo): se reescribe desde la plantilla, porque los ajustes del kit se prueban en conjunto.
  El archivo anterior se guarda: `cdmp.ini.before-launcher` si era del jugador (una vez, nunca se pisa) o
  `cdmp.ini.bak` si era del launcher.
- **Nombre del jugador**: solo ASCII imprimible, sin comillas, 32 caracteres. El mod lee el ini en la página de
  códigos ANSI del PC.
- **Nombres de archivo de un paquete**: solo `winmm.dll`, `cdmp.asi`, `cdmp.ini.host` y `cdmp.ini.guest`. Una lista
  no puede hacer que el launcher escriba otra cosa.
- **Si la dirección de actualizaciones no contesta**, manda lo más nuevo que haya en el PC. Así una actualización
  bajada ayer no se deshace porque la carpeta del kit sea más vieja, y un kit descomprimido hoy sí gana a una copia
  anterior. Límite conocido: un paquete no lleva número de orden, así que si la dirección contesta, gana lo que
  ella liste aunque un kit pasado a mano sea más nuevo (para eso está `--offline`, o "off" en "Updates from").

## `package.txt` y las actualizaciones

El mismo texto describe un kit (lo genera `package_coop.ps1`), una release de GitHub (es el mismo archivo) y
cualquier otra dirección de actualizaciones (una lista, el más nuevo primero):

```
[package]
id=cdmp-coop-20261114-3663-b12
game=1.0.0.3663 0x6AF5D198
file=winmm.dll <sha256> [dónde]
file=cdmp.asi <sha256> [dónde]
file=cdmp.ini.host <sha256> [dónde]
file=cdmp.ini.guest <sha256> [dónde]
```

`[dónde]` es una dirección `https://` o una ruta relativa a la lista; sin él, el archivo está junto a la lista (así
son los kits y las releases). `game=` puede repetirse. Solo se descarga por https; una lista en la red no puede
nombrar un archivo de este PC.

La dirección sale, por este orden, de los ajustes del jugador ("Updates from"), de `cdmp_launcher.ini` del kit
(`[update] address=`) y de la que lleva compilada el launcher (las releases del repositorio). Puede ser una dirección
https o una carpeta compartida; si acaba en `/` o es una carpeta, se lee su `package.txt`. `off` en cualquiera de
las dos primeras desactiva la comprobación.

El SHA-256 protege de una descarga corrupta, no de un servidor comprometido: la lista viene del mismo sitio que los
archivos. Quien pueda publicar una release en el repositorio decide qué código ejecutan los jugadores. Firmar la
lista quedaría para más adelante.

## Comprobado y sin comprobar

Comprobado el 2026-11-14:
- `launcher` en ctest: reglas y trabajo con archivos sobre carpetas falsas en `%TEMP%`, incluido el botón Update
  contra una dirección de actualizaciones que es una carpeta (descargar sin tocar el juego, rechazar un archivo
  cambiado, quedarse con lo del PC si la dirección es para otra build, el orden "lo más nuevo primero").
- En este PC, con la ventana real: encuentra el juego por Steam (1.0.0.3663) y reconoce el mod instalado; contra
  una copia falsa de `bin64` (`--game-dir`, `--no-launch`): instalar, `cdmp.ini` de invitado y de anfitrión, copia
  del ini anterior, conservar un cambio hecho a mano, elegir amigo pegando un SteamID64 o un enlace, quitar el mod.
- Update en la ventana real (con `--press update`, sin ratón), el launcher solo, sin kit, y la "release" en una
  carpeta: baja el paquete, lo instala, escribe el ini y pasa de "not downloaded yet" a "installed, up to date".
  Con una versión anterior instalada y otra más nueva en la "release": dice "Update available", el botón grande pasa
  a "Update and join a friend" y `--press play` (con `--no-launch`) instala la nueva.
- La descarga con el código del launcher contra GitHub: un archivo de release de otro repositorio público, también
  por `releases/latest/download/`, con el SHA-256 esperado; y la dirección de este repositorio cuando era privado
  (404).
- **Update de verdad desde GitHub**, con el repositorio ya público y la release `b1`: el launcher solo (sin kit ni
  ajustes, solo su dirección compilada), contra una copia falsa de `bin64` y con `--press update`: baja los cuatro
  archivos en unos 5 s, los instala (mismos SHA-256 que el kit de este PC), escribe el ini y queda en "installed,
  up to date". Los archivos de la release en GitHub tienen el mismo SHA-256 que los del kit local.
- La release `b1` se publicó con la versión anterior del script (entonces `ci_release.ps1 -Publish`, tras un
  `build.ps1` aparte). De `publish_release.ps1` están probados: que se niega con archivos cambiados, el número
  siguiente (`b2`, mirando las releases) y el modo sin `-Publish`.
- Una copia limpia del repositorio en este PC (un clon más los cambios de entonces): con el compilador de VS 2022
  (MSVC 14.44, el de las builds de siempre) compila, pasan los 105 tests (2 se saltan solos), se baja el cargador
  ASI comprobando sus dos SHA-256 y se hace el kit. Con el de VS 2026 (MSVC 14.51) falla un test,
  `optd_guest_save_b` (`parse_slot_dir(L"slot1")`, `tests/optd_guest_save_b_tests.cpp:708`): es un fallo de ese
  compilador (`docs/research/msvc-14.51-miscompila.md`).

- **Las partidas** (14-11, tarde). En ctest (`launcher`), con carpetas falsas: la regla de línea del invitado, la
  del anfitrión con diarios hechos con el código del mod, las copias de seguridad (no repetir, las 10 más nuevas, una
  copia a la que le falta un archivo no cuenta), pasar ranuras de anfitrión y de invitado, `applied.txt`, lo que se
  deja fuera (guardado del invitado en una ranura del anfitrión, partida real más nueva que la sesión, archivo
  cortado, pareja escrita a destiempo, restos `slotN_new` / `slotN_backup`, dos cuentas, caja sin diario) y la
  vuelta atrás con un archivo de la ranura abierto por otro proceso (la ranura queda igual y no queda nada en la
  carpeta de preparación).
- **Ensayo con la ventana real sobre copias de los datos de este PC** (`--local-dir`): copia de `save`, copia de
  `cdmp_save_sandbox_prev` (la caja tal como la dejó la sesión de anfitrión del 14-11 a las 16:31, con autoguardados
  en `slot0` y `slot1`) y el diario real de esa sesión. La línea "Saves" dijo `slot 0 (saved 14 Nov 16:34) and
  slot 1 (saved 14 Nov 16:49)`; con "Keep progress", y también solo (ajuste por defecto), las dos ranuras de la copia
  quedaron byte a byte como las de la sesión, las otras tres sin tocar, `slot204` fuera, la copia de seguridad igual
  que la carpeta de antes y la carpeta de preparación vacía. Con un archivo de `slot0` abierto, Play no arrancó ni
  instaló nada y lo dijo; al repetir sin el archivo abierto pasó las ranuras, hizo la copia e instaló el mod. La
  carpeta real no se tocó (su archivo más nuevo sigue siendo del 8-11).

Sin comprobar:
- **Las partidas contra la carpeta real y en una sesión jugada**: que el juego cargue la ranura pasada (la del
  anfitrión no se ha cargado nunca después de una sesión con invitado), qué hace Steam Cloud, y el camino del
  invitado con generaciones de verdad (en este PC solo las hay de pruebas sin Steam, que el launcher no mira).
- **Pulsar los botones contra el juego real y arrancarlo** (`steam://`), y una sesión jugada desde el launcher.
- El paso de una release a la siguiente bajada de GitHub (solo hay una; con una carpeta sí se probó).
- Otro PC: otra escala de pantalla, Steam en otra ruta, SmartScreen o el antivirus con el exe sin firmar.

Fuera de esta versión: que el launcher se actualice a sí mismo (hay que bajar el exe nuevo de la release a mano;
un launcher viejo tampoco tiene lo de las partidas), saber qué amigos están jugando ahora (haría falta abrir la API
de Steam con el AppID del juego desde el launcher: sin probar), un historial de versiones para volver a una
anterior, y volver a una copia de seguridad de las partidas desde la ventana (se hace a mano: copiar la carpeta).
