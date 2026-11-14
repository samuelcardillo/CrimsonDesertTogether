# Objetos obsoletos en `build\`: MSBuild deja de recompilar tras un fallo de compilación en paralelo

2026-11-14. No depende de la build del juego. Es la causa de que la release `cdmp-coop-20261114-3663-b1` se cierre al
arrancar cuando se juega de anfitrión.

## Qué pasó

- 14:01, primera sesión de dos copias con el código posterior al kit v23: la copia anfitriona murió 15 s después de
  lanzarla, antes de la pantalla de título. `cdmp_crash.host_20261114-140136_*.dmp`: `0xC0000005` leyendo `0x30` en
  `cdmp.asi+0x9B66D`, el constructor de copia de `std::wstring`, llamado desde `session2::install`
  (`session2.cpp:909`: `host_save_configure({... loaded_config().s2_host_save_dir ...})`), en el hilo del mod.
- `session2.cpp` no había cambiado desde el 13-nov. Lo que cambió fue `Config` (`src/mod/config.h`): el 14-nov se le
  añadieron cinco `std::string` y un `uint32_t` (`s2_send_trace` … `s2_gimmick_take`) antes de `s2_host_save_dir`.
  `session2.obj` era del 13-nov 20:35 y **no se había vuelto a compilar**: seguía leyendo `s2_host_save_dir` en su
  sitio antiguo, donde ahora está `s2_gimmick_take`, cuyo valor por defecto es `"0"`. Leído como `std::wstring`, el
  texto corto `"0"` es un puntero `0x30` con capacidad 15: de ahí la lectura de `0x30`.
- Con el `cdmp.asi` exacto de la release `b1` (`5f782c0e`) y los ini normales de dos copias (los del kit), lo mismo:
  el anfitrión termina con `0xC0000005` antes del título (14:07, mismo volcado). **`b1` no sirve para ser anfitrión.**
  El kit v23 se libró por poco: su único campo nuevo en `Config` era un `bool` que cupo en un hueco de alineación.

## Por qué no se recompiló

MSBuild guarda, por cada fuente, la lista de archivos que leyó al compilarse
(`build\<proyecto>.dir\<Config>\<proyecto>.tlog\CL.read.1.tlog`: una sección `^FUENTE.CPP` y una línea por archivo
leído). Con esa lista decide si un `.obj` está al día. En `cdmp_mod`, **32 de las 121 fuentes tenían la sección
vacía**: para MSBuild no dependían de ninguna cabecera y solo se recompilaban si cambiaba el propio `.cpp`.
18 de sus objetos eran más viejos que alguna cabecera que incluyen (`protocol.h`, `troc_sniffer.h`, `config.h`,
`vehicle_follow.h`); los más viejos, del 5-nov. Los demás proyectos (pruebas, launcher, `cdmp_common`) estaban bien.

Las fechas de esos objetos van en grupos (5-nov 20:28 y 23:10–23:20, 13-nov 20:33–20:40 y 22:15, 14-nov 03:56 y
05:51), cada grupo de una misma tanda de compilación. Lo que encaja, sin haberlo reproducido aparte: se compila en
paralelo (`/MP`), un archivo de la tanda da error, y a los que sí compilaron se les queda el `.obj` nuevo pero la
lista se guarda vacía. En desarrollo un error de compilación es cosa de todos los días.

## Qué se ha hecho

- `scripts/build.ps1`, antes de compilar: borra el `.obj` de toda fuente cuya lista esté vacía, así se vuelve a
  compilar y queda registrada («Stale-object guard: N object(s)…»). Después de una compilación sin errores comprueba
  que no quede ninguna fuente sin lista y, si queda, falla. La primera vez recompiló 32 objetos (103 s con las
  pruebas); después, `CL.read.1.tlog` tiene las 121 secciones con sus dependencias.
- `scripts/build.ps1 -Clean`: borra todos los objetos de la configuración. `scripts/publish_release.ps1 -Publish` lo
  usa siempre: en una release no entra ningún objeto de una compilación anterior.
- Sin proteger: compilar a mano con `cmake --build` o desde el IDE sin pasar por `build.ps1`.

## Lo que esto cambia de lo ya publicado

- El kit v23 (`534d441d`) y todos los anteriores se compilaron con objetos obsoletos respecto a `protocol.h` y otras
  cabeceras. Funcionaron en el juego porque esos objetos no usaban lo que había cambiado, o por suerte de alineación.
  El primer `cdmp.asi` compilado entero desde el 5-nov es el de la tarde del 14-nov.
- Que pasen las 105 pruebas no lo detecta: las pruebas compilan sus propias copias de las fuentes, y la prueba sin
  juego (`cdmp_host.exe`) no llega a `session2::install` con escritura.
- Para leer un volcado: `python -I tools/porting/minidump_inspect.py <dmp> --out <json>` da el RVA dentro de
  `cdmp.asi`; con el `.pdb` de `build\bin\RelWithDebInfo` (o el `-symbols.zip` de la release) sale la función y la
  línea (dbghelp: `SymLoadModuleEx` + `SymFromAddr` / `SymGetLineFromAddr64`).
