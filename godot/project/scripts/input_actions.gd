class_name InputActions
extends RefCounted
## Todo o mapeamento de teclas do simulador, em uma tabela (regra 13).
##
## A regra pede um Input Map do Godot e nenhum keycode espalhado pelo código.
## Ele é montado aqui, em tempo de execução, a partir da tabela abaixo, em vez
## de escrito à mão na seção `[input]` do `project.godot` -- e a razão é que essa
## seção guarda cada tecla como um `Object(InputEventKey, ...)` de vinte campos,
## que ninguém revê e onde ninguém vê um conflito.
##
## A tabela é a fonte da verdade de três coisas ao mesmo tempo:
##
##   * o `InputMap` que o jogo consulta;
##   * o painel de ajuda dentro do jogo;
##   * `docs/gameplay/controls.md`, gerado por `scripts/dump_controls.gd`.
##
## Se as três divergirem é porque alguém escreveu uma tecla em outro lugar, e
## esse é exatamente o erro que isto existe para tornar impossível.
##
## O que MUDOU em relação aos atalhos do Milestone 5, e por quê: a regra 13
## atribui W/A/S/D/Q/E à atitude e I/J/K/L/U/O à translação, e essas dez teclas
## estavam ocupadas pela câmera, pela exposição e pelos interruptores de ótica.
## O voo ganha o teclado principal; as ferramentas técnicas descem para as teclas
## de função e para `Alt`. Nada foi removido.

## [ação, tecla, shift, ctrl, alt, grupo, descrição]
const BINDINGS := [
	# --- atitude (RCS em rotação) ---
	["pitch_up", KEY_W, false, false, false, "ATITUDE", "arfagem para cima"],
	["pitch_down", KEY_S, false, false, false, "ATITUDE", "arfagem para baixo"],
	["yaw_left", KEY_A, false, false, false, "ATITUDE", "guinada à esquerda"],
	["yaw_right", KEY_D, false, false, false, "ATITUDE", "guinada à direita"],
	["roll_left", KEY_Q, false, false, false, "ATITUDE", "rolagem à esquerda"],
	["roll_right", KEY_E, false, false, false, "ATITUDE", "rolagem à direita"],

	# --- translação (RCS em translação) ---
	["translate_forward", KEY_I, false, false, false, "TRANSLAÇÃO", "à frente"],
	["translate_back", KEY_K, false, false, false, "TRANSLAÇÃO", "atrás"],
	["translate_left", KEY_J, false, false, false, "TRANSLAÇÃO", "à esquerda"],
	["translate_right", KEY_L, false, false, false, "TRANSLAÇÃO", "à direita"],
	["translate_up", KEY_U, false, false, false, "TRANSLAÇÃO", "acima"],
	["translate_down", KEY_O, false, false, false, "TRANSLAÇÃO", "abaixo"],

	# --- motor principal ---
	["throttle_up", KEY_SHIFT, false, false, false, "MOTOR", "abre o acelerador"],
	["throttle_down", KEY_CTRL, false, false, false, "MOTOR", "fecha o acelerador"],
	["throttle_full", KEY_Z, false, false, false, "MOTOR", "acelerador cheio"],
	["engine_cutoff", KEY_X, false, false, false, "MOTOR", "corte do motor"],
	["engine_mode", KEY_G, false, false, false, "MOTOR", "modo IMPULSO / CRUZEIRO"],
	["rcs_toggle", KEY_V, false, false, false, "MOTOR", "RCS ligado / desligado"],
	["rcs_mode", KEY_B, false, false, false, "MOTOR", "RCS rotação / translação"],

	# --- apontamento ---
	["point_prograde", KEY_P, false, false, false, "APONTAMENTO", "prógrado"],
	["point_retrograde", KEY_P, true, false, false, "APONTAMENTO", "retrógrado"],
	["point_normal", KEY_N, false, false, false, "APONTAMENTO", "normal"],
	["point_anti_normal", KEY_N, true, false, false, "APONTAMENTO", "anti-normal"],
	["point_radial_out", KEY_R, false, false, false, "APONTAMENTO", "radial para fora"],
	["point_radial_in", KEY_R, true, false, false, "APONTAMENTO", "radial para dentro"],
	["point_target", KEY_T, false, false, false, "APONTAMENTO", "para o alvo"],
	["point_anti_target", KEY_T, true, false, false, "APONTAMENTO", "contra o alvo"],
	["point_hold", KEY_0, false, false, false, "APONTAMENTO", "manter atitude"],

	# --- câmera ---
	["camera_cycle", KEY_C, false, false, false, "CÂMERA", "alterna o modo de câmera"],
	["camera_recentre", KEY_HOME, false, false, false, "CÂMERA", "recentra o olhar"],
	["camera_free_look", KEY_ALT, false, false, false, "CÂMERA", "olhar em volta (mantido)"],
	["camera_zoom_in", KEY_BRACKETRIGHT, false, false, false, "CÂMERA", "aproxima"],
	["camera_zoom_out", KEY_BRACKETLEFT, false, false, false, "CÂMERA", "afasta"],
	["camera_focus_next", KEY_F, false, false, false, "CÂMERA", "foca o próximo corpo"],

	# --- alvo e missão ---
	["target_next", KEY_APOSTROPHE, false, false, false, "MISSÃO", "próximo alvo"],
	["target_prev", KEY_SEMICOLON, false, false, false, "MISSÃO", "alvo anterior"],
	["mission_plan", KEY_J, true, false, false, "MISSÃO", "planeja a transferência"],
	["mission_execute", KEY_ENTER, false, false, false, "MISSÃO", "executa o plano"],
	["mission_abort", KEY_K, true, false, false, "MISSÃO", "ABORTA a missão"],
	["nav_panel", KEY_TAB, false, false, false, "MISSÃO", "computador de navegação"],
	["orbit_map", KEY_M, false, false, false, "MISSÃO", "mapa orbital"],
	["map_mode", KEY_M, true, false, false, "MISSÃO", "mapa local / sistema solar"],

	# --- tempo ---
	["warp_up", KEY_PERIOD, false, false, false, "TEMPO", "sobe o time warp"],
	["warp_down", KEY_COMMA, false, false, false, "TEMPO", "desce o time warp"],
	["pause", KEY_SPACE, false, false, false, "TEMPO", "pausa"],
	["menu", KEY_ESCAPE, false, false, false, "TEMPO", "menu"],

	# --- interface ---
	["hud_cycle", KEY_QUOTELEFT, false, false, false, "INTERFACE", "HUD completo / mínimo / nenhum"],
	["debug_hud", KEY_F3, false, false, false, "INTERFACE", "HUD técnico (todos os números)"],
	["help", KEY_F1, false, false, false, "INTERFACE", "ajuda dos controles"],
	["exposure_up", KEY_PAGEUP, false, false, false, "INTERFACE", "exposição do céu +"],
	["exposure_down", KEY_PAGEDOWN, false, false, false, "INTERFACE", "exposição do céu -"],

	# --- ferramentas técnicas (preservadas do M5) ---
	["restart_orbit", KEY_F5, false, false, false, "TÉCNICO", "reinicia a órbita de partida"],
	["execution_model", KEY_F6, false, false, false, "TÉCNICO", "planejador: finito / piloto automático"],
	["visual_beta", KEY_F7, false, false, false, "TÉCNICO", "escada de β visual"],
	["body_scale", KEY_F8, false, false, false, "TÉCNICO", "exagero de escala dos corpos"],
	["cruise_burn", KEY_C, false, false, true, "TÉCNICO", "queima de cruzeiro (β relativístico)"],
	["optics_aberration", KEY_A, false, false, true, "TÉCNICO", "aberração liga/desliga"],
	["optics_doppler", KEY_D, false, false, true, "TÉCNICO", "Doppler liga/desliga"],
	["optics_beaming", KEY_B, false, false, true, "TÉCNICO", "beaming liga/desliga"],
	["optics_light_time", KEY_L, false, false, true, "TÉCNICO", "tempo de luz liga/desliga"],
]


## Registra tudo no `InputMap`. Idempotente: chamar duas vezes não duplica
## nada, o que importa porque a cena pode ser recarregada.
static func install() -> void:
	for binding in BINDINGS:
		var action: String = binding[0]
		if InputMap.has_action(action):
			InputMap.action_erase_events(action)
		else:
			InputMap.add_action(action)
		var event := InputEventKey.new()
		event.physical_keycode = binding[1]
		event.shift_pressed = binding[2]
		event.ctrl_pressed = binding[3]
		event.alt_pressed = binding[4]
		InputMap.action_add_event(action, event)


## O rótulo de uma ação, para a ajuda e para os instrumentos. Pergunta ao
## `InputMap`, e não à tabela, porque é o `InputMap` que o jogo consulta -- se
## alguém reatribuir uma tecla em tempo de execução a ajuda acompanha.
static func label(action: String) -> String:
	if not InputMap.has_action(action):
		return "?"
	for event in InputMap.action_get_events(action):
		if event is InputEventKey:
			var key := event as InputEventKey
			var parts: Array[String] = []
			if key.alt_pressed:
				parts.append("Alt")
			if key.ctrl_pressed:
				parts.append("Ctrl")
			if key.shift_pressed:
				parts.append("Shift")
			parts.append(OS.get_keycode_string(key.physical_keycode))
			return "+".join(parts)
	return "?"


## Agrupado, para o painel de ajuda e para a documentação.
static func by_group() -> Dictionary:
	var groups: Dictionary = {}
	for binding in BINDINGS:
		var group: String = binding[5]
		if not groups.has(group):
			groups[group] = []
		groups[group].append({"action": binding[0], "key": label(binding[0]),
			"description": binding[6]})
	return groups


## `is_action_pressed` com casamento EXATO de modificadores.
##
## Sem isto `point_prograde` (P) dispararia com Shift+P junto de
## `point_retrograde`, e o piloto que pediu retrógrado receberia os dois
## comandos -- o último a ser tratado ganhando, o que é o pior tipo de bug de
## entrada, porque depende da ordem do `match`.
static func pressed_exact(event: InputEvent, action: String) -> bool:
	return event.is_action_pressed(action, false, true)
