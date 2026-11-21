# Lo que se usó para el port a 1.0.0.3732 (21-nov-2026), para el siguiente

`docs/research/port-3732.md` cuenta el port. Aquí queda lo que no es del juego sino del método, tal como se usó:

- `PORT_BRIEF.md`: el encargo común que recibió cada uno de los ocho subagentes (un grupo de módulos cada uno): la
  decisión de reapuntar en el sitio, las herramientas (`../p3732lib.py`), el método por tipo de valor, lo que entrega
  cada grupo (`map.json`, `verify.py`) y las reglas. Las rutas de scratchpad que nombra eran las de aquella sesión.
- `leadcheck.py`: el cruce del coordinador sobre los `map.json` de los grupos (la misma dirección vieja con la misma
  nueva en todos, tipos, direcciones viejas que siguen en el código) y la ejecución de cada `verify.py`. Los avisos
  «NOT A FUNCTION START» son casi todos trampolines y funciones hoja sin `.pdata`: se leen, no se obedecen.
- `rename.py`: el renombrado mecánico final de los identificadores `3663` a `3732` en el código (no en comentarios;
  no toca literales hexadecimales). Los identificadores con el número pegado a otra cifra (`kVf23663`) se le escapan:
  hubo que renombrar siete a mano. Después, `generate_native_opcode.py` y compilar.

Grupos de aquel día (archivos de `src/mod/` salvo que se diga): núcleo y transporte (`profile_s2.h`, `server`,
`guest`, `session2`, guardas, `guest_save_host`, `manager_probe`, `game_errors`); campo, identidad y reloj
(`child_hold`, `seq_marker`, `pivot_guard`, `field_share`, `identity_fix`, `clock_gate`); observador y clima
(`observer_milestone`, `observer_stage`, `interaction_condition`, `remote_visible`, `focus_follow`,
`scene_level_share`, `catch_seq`, `weather_sync`); usuario 2 e interfaz (`user2_funnels`, `ui_pause`, `guest_s3`,
`docked_gimmicks`, `tree_bend`, `remote_rope`, `observer_scene_copy`); lector y movimiento (`build_profile.h`,
`character_profile.h`, `player_reader`, `horse_diag`, `remote_interp`, `remote_motion`, `combat_follow`, más la
comparación de los lectores de cargas útiles); escenas (`guest_seq_*`, `guest_x_veto`); tiendas y casas
(`interact_gate`, `store_gate`, `guest_x_interaction`, `guest_shop_window`, `shop_ui_trace`, `housing_*`); barcos, mar
y monturas (`vehicle_follow`, `ride_null_guard`, `sea_sync`, `gimmick_relay`, `create_retry`, `mount_sync`).

**Lo primero la próxima vez:** copiar el `.exe` nuevo a `backups/exe/` (el de 3732 ya está). Con dos ejecutables el
mapa de direcciones se puede sacar función a función y casi todo esto sobra.
