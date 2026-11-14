# MSVC 14.51 (Visual Studio 2026) genera código incorrecto: `optd_guest_save_b` falla

2026-11-14. No depende de la build del juego.

**Causa.** Es un fallo del optimizador de MSVC 14.51, no un fallo nuestro ni comportamiento indefinido en
`parse_slot_dir`, `SlotDir` o el test. En `tests/optd_guest_save_b_tests.cpp:708` la expresión
`parse_slot_dir(L"slot1")->slot == 1 && parse_slot_dir(L"slot1")->kind == D::Plain` expande dos veces la función en
línea con el literal como argumento. La primera copia lee `name[4]` (el `'1'`) en `L"slot1"+8`; en la segunda el
compilador pierde el desplazamiento y lee `L"slot1"+0`, es decir `name[0]` (la `'s'`). Como `'s'` no es un dígito,
esa copia toma el camino de "no es una carpeta de slot" y compara el byte `kind` de un `optional` que nunca se
construyó: basura de la pila. `slot` sale bien (1); `kind` es lo que hubiera en la pila, y el test falla cuando no
es 0. Con `/Od` el mismo código es correcto.

| | Visual Studio | Toolset MSVC | `cl` / `c2.dll` | Resultado |
|---|---|---|---|---|
| Mal | Community 2026, 18.11.12328.437 | 14.51.36231 | 19.51.36260 | falla `optd_guest_save_b` (1 de 105) |
| Bien | Build Tools 2022, 17.14.37827.9 | 14.44.35207 | 19.44.35229 | pasan los 105 |

## Pruebas

- Código generado por 19.51.36260 con `/O2 /Ob1` (RelWithDebInfo) para la segunda copia, dentro de `test_slots`:

  ```asm
  movzx ecx, WORD PTR L"slot1"       ; name[0]; la primera copia lee L"slot1"+8
  sub   cx, 48
  movzx edx, BYTE PTR $T140[rsp+4]   ; kind de un optional sin construir
  cmp   cx, 9
  cmovbe edx, r12d                   ; r12d = 0 = DirKind::Plain
  ```

  19.51 despliega el bucle (sabe que el literal mide 5) y convierte la rama en `cmov`; ahí pierde el `+8`.
  19.44.35229 deja el bucle en las dos copias (`movzx r9d, WORD PTR [rdx+r14]`, con `rdx` desde 8).
- No es comportamiento indefinido: `name[4]` está dentro de un `wstring_view` de 5 caracteres y el `optional` tiene
  valor. Además, con la función marcada `constexpr`, el mismo 19.51.36260 acepta la expresión en un `static_assert`
  (la evaluación constante rechaza el comportamiento indefinido) y la falla en ejecución dentro del mismo programa.
- Reproducción mínima, solo con cabeceras estándar: `docs/research/data/msvc-14.51-repro.cpp` (el comando está en
  su cabecera). Con 19.51.36260 imprime `MISCOMPILED` con `/O2`, `/O2 /Ob1` y `/Ox`, y `ok` con `/Od`, `/O1`,
  `/O2 /Ob0` y `/O2 /d2SSAOptimizer-`; con 19.44.35229 imprime `ok` con todas. Que desaparezca al desactivar el
  optimizador SSA (opción no documentada) o la expansión en línea señala a ese paso del backend (`c2.dll`).
- Depende mucho de la forma del código. Hace falta que la función que llama tenga además un objeto local con
  destructor (en el test, el `std::vector<SlotSeen>` de más abajo; sin él sale bien) y que el resultado se use en
  el `if` del `CHECK`: guardado en un `bool` sale bien, y añadir un `printf` después de la línea 708 también lo
  arregla. Por eso `slot108`, `slot0`, `slot2_new` y `slot2_backup` pasan.

## Alcance

- En el mod, `guest_save_guest.cpp:227` llama a `parse_slot_dir` con un nombre leído del disco, no con un literal.
  Compilado ese fichero con 19.51.36260 y las opciones de `cdmp_mod`, la función queda fuera de línea y su código es
  correcto (lee `[name + i*2]` desde `i = 4` y `[name + 8]`). Esta llamada concreta no está afectada.
- Eso no hace fiable el resto: el fallo es del compilador y puede aparecer en cualquier otra función en línea a la
  que le llegue una constante. Solo se ha mirado esta. Un `cdmp.asi` compilado con MSVC 14.51 no se debe repartir.
- El aviso `C4244` de `<optional>` (`_Ty=int` a `unsigned char`) que da ese compilador al compilar `cdmp_common` no
  tiene relación: viene de `src/common/troc_script.cpp:47`, `type_size` devuelve `1`, `2`, `4`, `8` (`int`) como
  `std::optional<uint8_t>`. Los valores caben; la STL de 14.51 solo lo avisa donde la de 14.44 no.

## Qué se ha hecho

- `scripts/build.ps1`: una carpeta de build nueva se configura con Visual Studio 2022 aunque haya también uno más
  nuevo (antes, `vswhere -latest` elegía 2026 en este PC). Si solo hay Visual Studio 2026, o si `build\` ya estaba
  configurada con el generador `Visual Studio 18 2026`, se niega con un mensaje que apunta aquí. `-AllowVS2026` lo
  usa de todos modos; sirve para probar una actualización del compilador.
- El test se queda como está: es correcto, y con ese compilador su fallo es la señal de que el compilador genera
  código incorrecto. No se ha añadido `/d2SSAOptimizer-` al proyecto: evita este caso, pero es una opción sin
  documentar y no garantiza nada sobre el resto del código.
- Las releases se compilan con `scripts/build.ps1`, así que quedan cubiertas por la misma negativa.
- Sin proteger: un `cmake -G "Visual Studio 18 2026"` hecho a mano sigue compilando con 14.51 sin avisar.

## Cómo volver a comprobarlo

Con una actualización de Visual Studio 2026, desde su consola "x64 Native Tools":
`cl /nologo /std:c++20 /EHsc /O2 docs\research\data\msvc-14.51-repro.cpp` y ejecutar el `.exe`. Si imprime `ok`,
falta la prueba completa en una carpeta aparte, sin tocar `build\`: `cmake -S . -B out\vs2026 -G "Visual Studio 18
2026" -A x64 -Wno-dev`, `cmake --build out\vs2026 --config RelWithDebInfo --parallel` y `ctest --test-dir out\vs2026
-C RelWithDebInfo` (con el `cmake` de `<VS 2026>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`). Solo si
pasan los 105 tiene sentido quitar la negativa de `build.ps1`. El fallo no está comunicado a Microsoft.
