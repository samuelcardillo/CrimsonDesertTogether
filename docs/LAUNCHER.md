# Launcher de jugadores (`cdmp_launcher.exe`)

Hecho el 2026-11-14; el mismo día se le añadió la actualización desde GitHub. Instala o actualiza el mod en la
carpeta del juego, arranca el juego con el mod o sin él y escribe con quién se juega. No tiene código del juego ni
ganchos: solo descarga y copia archivos, escribe `cdmp.ini` y lanza `steam://rungameid/3321460`. Lo que ve el jugador
está en inglés.

## Dónde está

| Qué | Archivo |
|---|---|
| Reglas, sin Windows (se prueban solas) | `tools/launcher/launcher_logic.h` |
| Archivos, registro, descarga | `tools/launcher/launcher_core.cpp` |
| Ventana (Dear ImGui sobre Direct3D 11) | `tools/launcher/main.cpp` |
| Pruebas | `tests/launcher_tests.cpp` (ctest `launcher`) |
| En el kit | `scripts/package_coop.ps1` mete el exe, `package.txt` y `kit/coop/cdmp_launcher.ini` |
| Publicación de una release (desde un PC) | `scripts/publish_release.ps1`, `scripts/fetch_loader.ps1` |
| Para qué build del juego es el kit | `kit/coop/game-builds.txt` |

Para probarlo sin tocar el juego real:
`cdmp_launcher.exe --game-dir <bin64 falso> --data-dir <carpeta> --bundle <carpeta del kit> --no-launch`.
Un `bin64` falso sirve con los primeros 4 KB del `CrimsonDesert.exe` real (de ahí lee la build). `--offline` no
pregunta a la dirección de actualizaciones (para probar un kit local más nuevo que la última release) y
`--press update|play` pulsa ese botón en cuanto termina de mirar el PC, para pruebas sin ratón.

## De dónde sale el mod: se publica desde el PC de desarrollo, el launcher descarga

Los jugadores no tienen compilador (las VS Build Tools son varios GB), así que el mod no se compila en su PC. Se
compila en el PC de desarrollo y se publica como release del repositorio en GitHub; el launcher baja esa release.
En GitHub no se compila nada.

1. Con el commit ya subido: `.\scripts\publish_release.ps1 -Publish`. Se niega si hay archivos con cambios o si el
   commit no está en el remoto. Después ejecuta `scripts/build.ps1` (compila, pone en el mod el id de ese commit y
   pasa los tests: si falla uno, no se publica), baja el cargador ASI si falta (`fetch_loader.ps1`, comprobando su
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
`cdmp-coop-20261114-3663-b1` (commit `75bc963`), ya se publicó así.

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

Sin comprobar:
- **Una publicación entera con `publish_release.ps1 -Publish`** (compilar, kit y release en un solo paso).
- **Pulsar los botones contra el juego real y arrancarlo** (`steam://`), y una sesión jugada desde el launcher.
- El paso de una release a la siguiente bajada de GitHub (solo hay una; con una carpeta sí se probó).
- Otro PC: otra escala de pantalla, Steam en otra ruta, SmartScreen o el antivirus con el exe sin firmar.

Fuera de esta versión: que el launcher se actualice a sí mismo (hay que bajar el exe nuevo de la release a mano),
saber qué amigos están jugando ahora (haría falta abrir la API de Steam con el AppID del juego desde el launcher:
sin probar), un historial de versiones para volver a una anterior, y los pasos de después de la sesión
(`apply-guest-progress.ps1`, `host_save_restore.ps1`).
